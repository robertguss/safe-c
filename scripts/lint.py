#!/usr/bin/env python3
"""Lint application C against subset v1, or run the fixture self-test."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from tools import CLANG_FLAGS, ROOT, ToolError, prepend_local_bin, repo_rel, which_tool

SEMGREP_CONFIG = ROOT / "lint" / "semgrep" / "subset-v1.yml"
CLANG_TIDY_CONFIG = ROOT / ".clang-tidy"
WAIVERS = ROOT / "waivers.toml"

EXCLUDED_PREFIXES = ("stdlib/", "adapters/")
APP_SKIP_DIRS = {
    ".git",
    "stdlib",
    "adapters",
    "tests",
    "fuzz",
    "docs",
    "research-wiki",
    "__pycache__",
}

RULE_IDS = (
    "R1-no-subscript",
    "R2-no-ptr-arith",
    "R3-no-char-ptr",
    "R4-no-varargs",
    "R5-no-malloc-free",
    "R6-no-raw-array",
    "R7-no-vla",
    "R8-no-goto",
    "R9-no-errno",
    "R10-no-recursion",
    "R11-no-implicit-conversion",
    "R12-no-alloca",
    "R13-unchecked-result",
    "tidy-narrowing",
)
RULE_IDS_BY_LENGTH = tuple(sorted(RULE_IDS, key=len, reverse=True))

R11_FLAGS = {
    "conversion",
    "sign-conversion",
    "float-conversion",
    "implicit-int-conversion",
    "implicit-float-conversion",
    "shorten-64-to-32",
}

DIAG_LINE = re.compile(r":\d+:\d+:\s+(?:error|warning):")
BRACKET_FLAGS = re.compile(r"\[([^\]]+)\]\s*$")
HEAP_CALL = re.compile(r"\b(malloc|calloc|realloc|free)\s*\(")
ALLOCA_CALL = re.compile(r"\b(alloca|__builtin_alloca)\s*\(")
CHAR_PTR = re.compile(r"(?:const\s+)?char(?:\s+const)?\s*\*")

REQUIRED_TOOLS = (
    ("semgrep", ("semgrep",)),
    ("clang", ("clang", "clang-18")),
    ("clang-tidy", ("clang-tidy", "clang-tidy-18")),
)


def is_excluded(path: Path) -> bool:
    rel = repo_rel(path)
    return any(rel == prefix.rstrip("/") or rel.startswith(prefix) for prefix in EXCLUDED_PREFIXES)


def require_tools() -> dict[str, str]:
    prepend_local_bin()
    found: dict[str, str] = {}
    missing: list[str] = []
    for label, names in REQUIRED_TOOLS:
        path = which_tool(names)
        if path is None:
            missing.append(label)
        else:
            found[label] = path
    if missing:
        listed = ", ".join(missing)
        raise ToolError(
            f"error: missing required tool: {listed}\n"
            "Install the tools listed in AGENTS.md and retry."
        )
    return found


def strip_comments_and_strings(src: str) -> str:
    out: list[str] = []
    i = 0
    n = len(src)
    while i < n:
        ch = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            while i < n and src[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if ch == "/" and nxt == "*":
            out.append("  ")
            i += 2
            while i + 1 < n and not (src[i] == "*" and src[i + 1] == "/"):
                out.append("\n" if src[i] == "\n" else " ")
                i += 1
            if i + 1 < n:
                out.append("  ")
                i += 2
            continue
        if ch == '"':
            out.append(" ")
            i += 1
            while i < n and src[i] != '"':
                if src[i] == "\\" and i + 1 < n:
                    out.append("  ")
                    i += 2
                    continue
                out.append("\n" if src[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append(" ")
                i += 1
            continue
        if ch == "'":
            out.append(" ")
            i += 1
            while i < n and src[i] != "'":
                if src[i] == "\\" and i + 1 < n:
                    out.append("  ")
                    i += 2
                    continue
                out.append(" ")
                i += 1
            if i < n:
                out.append(" ")
                i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def warning_names(bracket: str) -> list[str]:
    names: list[str] = []
    for part in bracket.split(","):
        token = part.strip()
        if token in {"Werror", "-Werror", "warnings-as-errors"}:
            continue
        if token.startswith("-W"):
            names.append(token[2:])
            continue
        if token.startswith("W") and token != "Werror":
            names.append(token[1:])
            continue
        names.append(token)
    return names


def parse_clang_rules(output: str) -> set[str]:
    rules: set[str] = set()
    for line in output.splitlines():
        if not DIAG_LINE.search(line):
            continue
        match = BRACKET_FLAGS.search(line)
        if match is None:
            continue
        for name in warning_names(match.group(1)):
            if name == "vla":
                rules.add("R7-no-vla")
            elif name in R11_FLAGS:
                rules.add("R11-no-implicit-conversion")
    return rules


def parse_tidy_rules(output: str) -> set[str]:
    rules: set[str] = set()
    for line in output.splitlines():
        if not DIAG_LINE.search(line):
            continue
        match = BRACKET_FLAGS.search(line)
        if match is None:
            continue
        for name in warning_names(match.group(1)):
            if name == "misc-no-recursion":
                rules.add("R10-no-recursion")
            elif name == "bugprone-narrowing-conversions":
                rules.add("tidy-narrowing")
    return rules


def normalize_semgrep_id(check_id: str) -> str | None:
    last = check_id.split(".")[-1]
    if last in RULE_IDS:
        return last
    return None


def scan_stripped(src: str) -> set[str]:
    rules: set[str] = set()
    text = strip_comments_and_strings(src)
    if HEAP_CALL.search(text):
        rules.add("R5-no-malloc-free")
    if ALLOCA_CALL.search(text):
        rules.add("R12-no-alloca")
    if CHAR_PTR.search(text):
        rules.add("R3-no-char-ptr")
    return rules


def run_checked(
    argv: list[str],
    tmp_path: Path,
    *,
    ok_codes: tuple[int, ...] = (0,),
) -> subprocess.CompletedProcess[str]:
    try:
        with tmp_path.open("w", encoding="utf-8") as handle:
            proc = subprocess.run(
                argv,
                cwd=ROOT,
                text=True,
                stdout=handle,
                stderr=subprocess.STDOUT,
                check=False,
            )
    except FileNotFoundError as exc:
        raise ToolError(f"error: missing required tool: {argv[0]}") from exc
    proc.stdout = tmp_path.read_text(encoding="utf-8", errors="replace")
    proc.stderr = ""
    if proc.returncode not in ok_codes:
        return proc
    return proc


def collect(path: Path, tools: dict[str, str], tmpdir: Path) -> set[str]:
    if is_excluded(path):
        return set()
    rules: set[str] = set()
    src = path.read_text(encoding="utf-8")
    rules.update(scan_stripped(src))

    sem_path = tmpdir / "semgrep.json"
    sem = run_checked(
        [
            tools["semgrep"],
            "--config",
            str(SEMGREP_CONFIG),
            "--metrics=off",
            "--json",
            "--quiet",
            str(path),
        ],
        sem_path,
        ok_codes=(0, 1),
    )
    if sem.returncode not in (0, 1):
        sys.stderr.write(sem.stdout)
        raise ToolError(f"error: semgrep failed on {repo_rel(path)} (exit {sem.returncode})")
    try:
        data = json.loads(sem.stdout or "{}")
    except json.JSONDecodeError as exc:
        raise ToolError(
            "error: semgrep did not emit JSON. Is semgrep installed and working?\n"
            f"Install semgrep as listed in AGENTS.md and retry. ({exc})"
        ) from exc
    fatal = [err for err in (data.get("errors") or []) if err.get("level") == "error"]
    if fatal:
        for err in fatal:
            sys.stderr.write(err.get("message", "semgrep error") + "\n")
        raise ToolError(f"error: semgrep reported a rule or parse error on {repo_rel(path)}")
    for result in data.get("results") or []:
        ident = normalize_semgrep_id(str(result.get("check_id") or ""))
        if ident is not None:
            rules.add(ident)

    tidy_path = tmpdir / "tidy.out"
    tidy = run_checked(
        [
            tools["clang-tidy"],
            f"--config-file={CLANG_TIDY_CONFIG}",
            str(path),
            "--",
            "-std=c11",
        ],
        tidy_path,
        ok_codes=(0, 1),
    )
    if tidy.returncode not in (0, 1):
        sys.stderr.write(tidy.stdout)
        raise ToolError(f"error: clang-tidy failed on {repo_rel(path)} (exit {tidy.returncode})")
    rules.update(parse_tidy_rules(tidy.stdout))

    cc_path = tmpdir / "clang.out"
    cc = run_checked(
        [tools["clang"], *CLANG_FLAGS, "-fsyntax-only", str(path)],
        cc_path,
        ok_codes=(0, 1),
    )
    clang_rules = parse_clang_rules(cc.stdout)
    rules.update(clang_rules)
    if cc.returncode != 0 and not clang_rules:
        sys.stderr.write(cc.stdout)
        if not rules:
            raise ToolError(f"error: clang failed on {repo_rel(path)} (exit {cc.returncode})")

    return rules


def emit(rules: set[str]) -> None:
    for ident in sorted(rules):
        print(f"lint-rule {ident}")


def application_sources() -> list[Path]:
    files: list[Path] = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [name for name in dirnames if name not in APP_SKIP_DIRS]
        base = Path(dirpath)
        for name in filenames:
            if name.endswith((".c", ".h")):
                files.append(base / name)
    return sorted(files)


def expected_rule_id(filename: str) -> str | None:
    stem = filename[:-2] if filename.endswith(".c") else filename
    for ident in RULE_IDS_BY_LENGTH:
        if stem == ident or stem.startswith(ident + "-"):
            return ident
    return None


def lint_paths(paths: list[Path], tools: dict[str, str]) -> int:
    status = 0
    with tempfile.TemporaryDirectory(prefix="safe-c-lint-") as tmp:
        tmpdir = Path(tmp)
        if not paths:
            print("lint: no application C sources")
            return 0
        for path in paths:
            if is_excluded(path):
                print(f"skip excluded {repo_rel(path)}")
                continue
            try:
                rules = collect(path, tools, tmpdir)
            except ToolError as exc:
                print(str(exc), file=sys.stderr)
                print(f"fail {repo_rel(path)}")
                status = 1
                continue
            if rules:
                print(f"fail {repo_rel(path)}")
                emit(rules)
                status = 1
            else:
                print(f"pass {repo_rel(path)}")
    return status


def selftest(tools: dict[str, str]) -> int:
    status = 0
    bad_dir = ROOT / "tests" / "lint" / "bad"
    good_dir = ROOT / "tests" / "lint" / "good"
    with tempfile.TemporaryDirectory(prefix="safe-c-lint-") as tmp:
        tmpdir = Path(tmp)
        for path in sorted(bad_dir.glob("*.c")):
            ident = expected_rule_id(path.name)
            if ident is None:
                print(f"bad file has no rule id: {repo_rel(path)}", file=sys.stderr)
                status = 1
                continue
            try:
                rules = collect(path, tools, tmpdir)
            except ToolError as exc:
                print(str(exc), file=sys.stderr)
                print(f"missing {ident} {repo_rel(path)}", file=sys.stderr)
                status = 1
                continue
            if ident in rules:
                print(f"bad {ident} {repo_rel(path)}")
            else:
                print(f"missing {ident} {repo_rel(path)}", file=sys.stderr)
                emit(rules)
                status = 1
        for path in sorted(good_dir.glob("*.c")):
            try:
                rules = collect(path, tools, tmpdir)
            except ToolError as exc:
                print(str(exc), file=sys.stderr)
                print(f"clean file failed {repo_rel(path)}", file=sys.stderr)
                status = 1
                continue
            if rules:
                print(f"clean file failed {repo_rel(path)}", file=sys.stderr)
                emit(rules)
                status = 1
            else:
                print(f"pass {repo_rel(path)}")
    if status == 0:
        print("lint self-test passed")
    return status


def check_waivers() -> int:
    from waivers import load_waivers, report_waivers

    report_waivers(load_waivers(WAIVERS))
    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--selftest",
        action="store_true",
        help="run tests/lint fixtures instead of application sources",
    )
    parser.add_argument(
        "--waivers",
        action="store_true",
        help="fail on expired waivers in waivers.toml",
    )
    parser.add_argument(
        "--gates",
        action="store_true",
        help="run application lint, then the waiver expiry check",
    )
    parser.add_argument("paths", nargs="*", help="optional files to lint")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        if args.waivers and not args.gates and not args.selftest and not args.paths:
            return check_waivers()
        tools = require_tools()
        if args.selftest:
            if args.paths:
                print("usage: scripts/lint.py --selftest", file=sys.stderr)
                return 2
            return selftest(tools)
        if args.gates:
            status = lint_paths(application_sources(), tools)
            waiver_status = check_waivers()
            return status or waiver_status
        if args.paths:
            return lint_paths([Path(item) for item in args.paths], tools)
        return lint_paths(application_sources(), tools)
    except ToolError as exc:
        print(str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
