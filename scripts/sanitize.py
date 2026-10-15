#!/usr/bin/env python3
"""Run sanitizer tests, production fuzz harnesses, and negative gate fixtures."""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from tools import CLANG_FLAGS, ROOT, ToolError, prepend_local_bin, which_tool

SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
FUZZ_FLAGS = ["-fsanitize=fuzzer,address,undefined", "-fno-sanitize-recover=all"]
FIXTURE_DIRS = {"lint", "gates"}
FUZZ_PROBE = """#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    (void)data; (void)size; return 0;
}
"""
TOOLCHAIN_HELP = (
    "Use a clang toolchain with ASan/UBSan/libFuzzer. On macOS install Homebrew llvm "
    "and set LLVM to its prefix. On Linux install compiler-rt, libFuzzer and the "
    "GCC C++ development libraries (g++); if needed set LIBRARY_PATH to the directory "
    "containing libstdc++.so."
)


class CommandFailed(RuntimeError):
    def __init__(self, argv, returncode, output=""):
        self.argv = argv
        self.returncode = returncode
        detail = f"{output}\n" if output else ""
        super().__init__(f"{detail}error: command failed ({returncode}): {shlex.join(argv)}")


def resolve_src(path: Path) -> Path:
    return path if path.is_absolute() else ROOT / path


def relpath(path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(ROOT).as_posix()
    except ValueError:
        return resolved.as_posix()


def cflags():
    raw = os.environ.get("CFLAGS")
    if not raw:
        return [*CLANG_FLAGS, "-g", "-O1"]
    try:
        return shlex.split(raw)
    except ValueError as exc:
        raise ToolError(f"error: invalid CFLAGS: {exc}") from exc


def run_cmd(argv, *, check=True, capture=False, timeout=None):
    result = subprocess.run(
        argv,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
        timeout=timeout,
    )
    if check and result.returncode:
        raise CommandFailed(argv, result.returncode, result.stdout or "")
    return result


def compiler():
    prepend_local_bin()
    selected = os.environ.get("CC")
    if not selected:
        llvm = os.environ.get("LLVM")
        if not llvm and sys.platform == "darwin":
            llvm = next((p for p in ("/opt/homebrew/opt/llvm", "/usr/local/opt/llvm")
                         if Path(p).is_dir()), None)
        selected = str(Path(llvm) / "bin/clang") if llvm else which_tool(("clang", "clang-18"))
    if not selected or not which_tool((selected,)):
        raise ToolError(f"error: missing required tool: {selected or 'clang'}. {TOOLCHAIN_HELP}")
    return selected


def fuzz_link_flags():
    # clang's C driver can omit GCC's library search path when linking libFuzzer.
    gxx = which_tool(("g++",)) if sys.platform.startswith("linux") else None
    if not gxx:
        return []
    result = run_cmd([gxx, "-print-file-name=libstdc++.so"], capture=True)
    library = Path(result.stdout.strip())
    if library.is_absolute() and library.is_file():
        return [f"-L{library.parent}"]
    return []


def build(cc, src, out, fuzz=False):
    out.parent.mkdir(parents=True, exist_ok=True)
    flags = cflags()
    flags += FUZZ_FLAGS + fuzz_link_flags() if fuzz else SAN_FLAGS
    result = run_cmd([cc, *flags, str(src), "-o", str(out)], check=False, capture=True)
    if result.returncode:
        raise ToolError(f"{result.stdout}\nerror: failed to build {src}. {TOOLCHAIN_HELP}")


def probe(cc, fuzz=False):
    with tempfile.TemporaryDirectory(prefix="safe-c-probe-") as tmp:
        src = Path(tmp) / "probe.c"
        out = Path(tmp) / "probe"
        src.write_text(FUZZ_PROBE if fuzz else "int main(void) { return 0; }\n")
        build(cc, src, out, fuzz)
        run_cmd([str(out), *(["-runs=1"] if fuzz else [])], capture=True)


def test_sources():
    tests = ROOT / "tests"
    if not tests.is_dir():
        return []
    found = []
    for src in tests.rglob("*_test.c"):
        if src.relative_to(tests).parts[0] in FIXTURE_DIRS:
            continue
        found.append(src)
    return sorted(found)


def fuzz_harnesses():
    fuzz = ROOT / "fuzz"
    if not fuzz.is_dir():
        return []
    return sorted(fuzz.glob("*_fuzz.c"))


def run_tests(cc):
    probe(cc)
    sources = test_sources()
    if not sources:
        print("test: no *_test.c sources", flush=True)
    for src in sources:
        out = ROOT / "build" / src.resolve().relative_to(ROOT).with_suffix("")
        build(cc, src, out)
        run_cmd([str(out)])
        print(f"pass {relpath(src)}", flush=True)


def fuzz_seconds(default):
    value = os.environ.get("FUZZ_SECONDS") or str(default)
    # libFuzzer interprets zero as unlimited, which would hang the gate.
    if not value.isascii() or not value.isdecimal() or int(value) < 1:
        raise ToolError("error: FUZZ_SECONDS must be a positive integer")
    return int(value)


def run_fuzz(cc, sources, seconds):
    probe(cc, fuzz=True)
    if not sources:
        print("fuzz: no fuzz/*_fuzz.c harnesses", flush=True)
    for src in sources:
        src = resolve_src(src)
        out = ROOT / "build/fuzz" / src.stem
        build(cc, src, out, fuzz=True)
        # Per-input default is 1200s; bound hangs to the campaign budget.
        run_cmd([str(out), f"-artifact_prefix={ROOT / 'build/fuzz'}/",
                 f"-max_total_time={seconds}", f"-timeout={seconds}"])


def selftest(cc):
    fixtures = (
        ("asan", "asan-canary.c", "ERROR: AddressSanitizer:"),
        ("ubsan", "ubsan-canary.c", "runtime error:"),
        ("fuzz", "canary_fuzz.c", "ERROR: AddressSanitizer:"),
    )
    for name, filename, diagnostic in fixtures:
        out = ROOT / "build/gates" / name
        build(cc, ROOT / "tests/gates" / filename, out, fuzz=name == "fuzz")
        args = []
        if name == "fuzz":
            # Replay the trigger so catching the canary never depends on random luck.
            seed = ROOT / "build/gates/fuzz-seed"
            seed.write_bytes(b"A")
            args = [str(seed), "-runs=1", f"-artifact_prefix={ROOT / 'build/gates'}/"]
        result = run_cmd([str(out), *args], check=False, capture=True, timeout=30)
        log = out.with_suffix(".log")
        log.write_text(result.stdout or "")
        if result.returncode == 0 or diagnostic not in (result.stdout or ""):
            raise ToolError(f"error: {name} canary was not caught with {diagnostic}; see {log}")
        print(f"{name} canary caught", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("test", "fuzz", "gate-selftest", "gates"))
    parser.add_argument("sources", nargs="*", type=Path)
    args = parser.parse_args()
    if args.sources and args.mode != "fuzz":
        parser.error("explicit sources are only supported for fuzz")
    cc = compiler()
    if args.mode in ("test", "gates"):
        run_tests(cc)
    if args.mode in ("gate-selftest", "gates"):
        selftest(cc)
    if args.mode in ("fuzz", "gates"):
        sources = args.sources or fuzz_harnesses()
        run_fuzz(cc, sources, fuzz_seconds(5 if args.mode == "gates" else 60))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ToolError, OSError, subprocess.TimeoutExpired) as exc:
        print(exc, file=sys.stderr)
        sys.exit(2)
    except CommandFailed as exc:
        print(exc, file=sys.stderr)
        sys.exit(1)
