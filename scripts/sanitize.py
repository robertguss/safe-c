#!/usr/bin/env python3
"""Run sanitizer tests, production fuzz harnesses, and negative gate fixtures."""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from lint import ROOT, ToolError, which_tool

DEFAULT_FLAGS = "-std=c11 -Werror -Wall -Wextra -Wvla -Wconversion -Wsign-conversion -g -O1"
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
FUZZ_FLAGS = ["-fsanitize=fuzzer,address,undefined", "-fno-sanitize-recover=all"]
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


def compiler():
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
    if gxx:
        result = subprocess.run([gxx, "-print-file-name=libstdc++.so"],
                                capture_output=True, text=True, check=True)
        library = Path(result.stdout.strip())
        if library.is_absolute() and library.is_file():
            return [f"-L{library.parent}"]
    return []


def build(cc, src, out, fuzz=False):
    out.parent.mkdir(parents=True, exist_ok=True)
    flags = shlex.split(os.environ.get("CFLAGS") or DEFAULT_FLAGS)
    flags += FUZZ_FLAGS + fuzz_link_flags() if fuzz else SAN_FLAGS
    result = subprocess.run([cc, *flags, str(src), "-o", str(out)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode:
        raise ToolError(f"{result.stdout}\nerror: failed to build {src}. {TOOLCHAIN_HELP}")


def probe(cc, fuzz=False):
    with tempfile.TemporaryDirectory(prefix="safe-c-probe-") as tmp:
        src = Path(tmp) / "probe.c"
        out = Path(tmp) / "probe"
        src.write_text(FUZZ_PROBE if fuzz else "int main(void) { return 0; }\n")
        build(cc, src, out, fuzz)
        subprocess.run([str(out), *(["-runs=1"] if fuzz else [])], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def run_tests(cc):
    probe(cc)
    sources = sorted(Path("tests").rglob("*_test.c"))
    if not sources:
        print("test: no *_test.c sources", flush=True)
    for src in sources:
        out = Path("build") / src.with_suffix("")
        build(cc, src, out)
        subprocess.run([str(out)], check=True)
        print(f"pass {src}", flush=True)


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
        out = Path("build/fuzz") / src.stem
        build(cc, src, out, fuzz=True)
        subprocess.run([str(out), "-artifact_prefix=build/fuzz/",
                        f"-max_total_time={seconds}"], check=True)


def selftest(cc):
    fixtures = (
        ("asan", "asan-canary.c", "ERROR: AddressSanitizer:"),
        ("ubsan", "ubsan-canary.c", "runtime error:"),
        ("fuzz", "canary_fuzz.c", "ERROR: AddressSanitizer:"),
    )
    for name, filename, diagnostic in fixtures:
        out = Path("build/gates") / name
        build(cc, Path("tests/gates") / filename, out, fuzz=name == "fuzz")
        args = []
        if name == "fuzz":
            # Replay the trigger so catching the canary never depends on random luck.
            seed = Path("build/gates/fuzz-seed")
            seed.write_bytes(b"A")
            args = [str(seed), "-runs=1", "-artifact_prefix=build/gates/"]
        result = subprocess.run([str(out), *args], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=30)
        log = out.with_suffix(".log")
        log.write_text(result.stdout)
        if result.returncode == 0 or diagnostic not in result.stdout:
            raise ToolError(f"error: {name} canary was not caught with {diagnostic}; see {log}")
        print(f"{name} canary caught", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("test", "fuzz", "gate-selftest", "gates"))
    parser.add_argument("sources", nargs="*", type=Path)
    args = parser.parse_args()
    if args.sources and args.mode != "fuzz":
        parser.error("explicit sources are only supported for fuzz")
    os.chdir(ROOT)
    cc = compiler()
    if args.mode in ("test", "gates"):
        run_tests(cc)
    if args.mode in ("gate-selftest", "gates"):
        selftest(cc)
    if args.mode in ("fuzz", "gates"):
        sources = args.sources or sorted(Path("fuzz").glob("*_fuzz.c"))
        run_fuzz(cc, sources, fuzz_seconds(5 if args.mode == "gates" else 60))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ToolError, OSError, ValueError, subprocess.TimeoutExpired) as exc:
        print(exc, file=sys.stderr)
        sys.exit(2)
    except subprocess.CalledProcessError as exc:
        if exc.stderr:
            print(exc.stderr.decode() if isinstance(exc.stderr, bytes) else exc.stderr, file=sys.stderr)
        print(f"error: command failed ({exc.returncode}): {shlex.join(exc.cmd)}", file=sys.stderr)
        sys.exit(1)
