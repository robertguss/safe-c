#!/usr/bin/env python3
"""Check every code example in docs/guide/ and docs/agent-prompt.md.

A fenced `c` block must say what it is, so none can rot unchecked:

  c good           compiles with the subset warning set and passes safec lint
  c avoid          the same, for code that is legal but discouraged
  c bad R1-...     compiles, and lint reports that rule id
  c adapter        compiles; adapters are outside the subset, so lint skips it
  c quote PATH     every line is a line of PATH, in order; a line that is only
                   `/* ... */` marks an elision
  c file=NAME      a file of the quickstart project (docs/guide/quickstart.md)

In the quickstart, `sh install` blocks run in the Safe C tree with $HOME/.local
replaced by a scratch prefix, then `sh project` blocks run in the project built
from the `file=` blocks, in document order. Any non-zero exit fails the check.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from lint import RULE_IDS
from tools import CLANG_FLAGS, ROOT, ToolError, prepend_local_bin, which_tool

DOCS = [*sorted((ROOT / "docs/guide").glob("*.md")), ROOT / "docs/agent-prompt.md"]
WORK = ROOT / "build/guide"  # --work overrides it
FENCE = re.compile(r"^```([^\n]*)\n(.*?)^```[ \t]*$", re.M | re.S)
PREFIX_PLACEHOLDER = "$HOME/.local"


@dataclass
class Block:
    doc: Path
    line: int
    lang: str
    tag: str
    arg: str
    body: str

    @property
    def where(self) -> str:
        return f"{self.doc.resolve().relative_to(ROOT)}:{self.line}"


def blocks_of(doc: Path) -> list[Block]:
    text = doc.read_text(encoding="utf-8")
    found = []
    for m in FENCE.finditer(text):
        words = m.group(1).split()
        lang = words[0] if words else ""
        tag, arg = "", ""
        if len(words) > 1:
            tag, _, arg = words[1].partition("=")
            if not arg and len(words) > 2:
                arg = words[2]
        line = text.count("\n", 0, m.start()) + 1
        found.append(Block(doc, line, lang, tag, arg, m.group(2)))
    return found


def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT, **kw)


def write_block(b: Block, index: int) -> Path:
    path = WORK / "blocks" / f"{b.doc.stem}-{index:02d}.c"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(b.body, encoding="utf-8")
    return path


def compile_block(b: Block, path: Path, strict: bool) -> str | None:
    flags = CLANG_FLAGS if strict else [f for f in CLANG_FLAGS if f != "-Werror"]
    clang = which_tool(("clang", "clang-18"))
    if clang is None:
        raise ToolError("error: missing required tool: clang")
    cc = run([clang, *flags, "-Iinclude", "-fsyntax-only", str(path)])
    if cc.returncode != 0:
        return f"{b.where}: does not compile\n{cc.stderr}"
    return None


def check_quote(b: Block) -> str | None:
    source = (ROOT / b.arg).read_text(encoding="utf-8").splitlines()
    pos = 0
    skipping = True  # the first line may be anywhere in the file
    for raw in b.body.splitlines():
        line = raw.rstrip()
        if not line:
            continue
        if line.strip() == "/* ... */":
            skipping = True
            continue
        start = pos
        while pos < len(source) and source[pos].rstrip() != line:
            if not skipping:
                return f"{b.where}: line is not the next line of {b.arg}: {line!r}"
            pos += 1
        if pos == len(source):
            return f"{b.where}: line not found in {b.arg} after line {start}: {line!r}"
        pos += 1
        skipping = False
    return None


def lint(paths: list[Path]) -> dict[Path, set[str]]:
    """Runs scripts/lint.py once over every file; maps each file to its rule ids."""
    if not paths:
        return {}
    out = run(["python3", "scripts/lint.py", *map(str, paths)])
    results: dict[Path, set[str]] = {}
    current: Path | None = None
    for line in out.stdout.splitlines():
        if line.startswith(("pass ", "fail ")):
            current = ROOT / line.split(" ", 1)[1]
            results[current] = set()
        elif line.startswith("lint-rule ") and current is not None:
            results[current].add(line.split(" ", 1)[1])
    if out.returncode not in (0, 1) or len(results) != len(paths):
        sys.stderr.write(out.stdout + out.stderr)
        raise ToolError("error: scripts/lint.py did not lint every guide example")
    return results


def quickstart(blocks: list[Block]) -> list[str]:
    errors: list[str] = []
    project = WORK / "project"
    prefix = WORK / "prefix"
    shutil.rmtree(project, ignore_errors=True)
    shutil.rmtree(prefix, ignore_errors=True)
    project.mkdir(parents=True)
    script = ["set -eu", f"cd {ROOT}"]
    in_project = False
    for b in blocks:
        if b.lang == "c" and b.tag == "file":
            target = project / b.arg
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(b.body, encoding="utf-8")
        elif b.lang == "sh" and b.tag == "install":
            script.append(b.body.replace(PREFIX_PLACEHOLDER, str(prefix)))
        elif b.lang == "sh" and b.tag == "project":
            if not in_project:
                script.append(f"cd {project}")
                in_project = True
            script.append(b.body.replace(PREFIX_PLACEHOLDER, str(prefix)))
    done = subprocess.run(["sh", "-c", "\n".join(script)], capture_output=True, text=True)
    if done.returncode != 0:
        errors.append(f"quickstart failed (exit {done.returncode})\n{done.stdout}{done.stderr}")
    return errors


def main() -> int:
    global WORK
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--no-lint", action="store_true",
                        help="skip safec lint and the quickstart run (no semgrep available)")
    parser.add_argument("--work", type=Path, default=WORK, help="scratch directory (inside the tree)")
    parser.add_argument("docs", nargs="*", type=Path, help="Markdown files (default: the guide and the prompt)")
    args = parser.parse_args()
    WORK = args.work.resolve()
    prepend_local_bin()
    shutil.rmtree(WORK / "blocks", ignore_errors=True)

    errors: list[str] = []
    to_lint: list[tuple[Block, Path]] = []
    counts = {"good": 0, "avoid": 0, "bad": 0, "adapter": 0, "quote": 0}
    quick: list[Block] = []
    for doc in [d.resolve() for d in args.docs] or DOCS:
        if not doc.exists():
            errors.append(f"{doc.relative_to(ROOT)}: missing")
            continue
        for index, b in enumerate(blocks_of(doc)):
            if doc.name == "quickstart.md" and (b.tag in ("file", "install", "project")):
                quick.append(b)
            elif b.lang == "c":
                if b.tag in ("good", "avoid"):
                    path = write_block(b, index)
                    err = compile_block(b, path, strict=True)
                    to_lint.append((b, path))
                elif b.tag == "bad":
                    if b.arg not in RULE_IDS:
                        errors.append(f"{b.where}: `c bad` needs a rule id from scripts/lint.py, such as R1-no-subscript")
                        continue
                    path = write_block(b, index)
                    err = compile_block(b, path, strict=False)
                    to_lint.append((b, path))
                elif b.tag == "adapter":
                    path = write_block(b, index)
                    err = compile_block(b, path, strict=True)
                elif b.tag == "quote":
                    err = check_quote(b)
                else:
                    err = (f"{b.where}: a `c` block must be tagged good, avoid, "
                           "bad <rule>, adapter, quote <path> or file=<name>")
                    errors.append(err)
                    continue
                counts[b.tag] += 1
                if err:
                    errors.append(err)

    if not args.no_lint:
        try:
            results = lint([p for _, p in to_lint])
        except ToolError as exc:
            print(exc, file=sys.stderr)
            return 1
        for b, path in to_lint:
            rules = results[path]
            if b.tag == "bad" and b.arg not in rules:
                errors.append(f"{b.where}: lint did not report {b.arg} (reported: {sorted(rules)})")
            elif b.tag != "bad" and rules:
                errors.append(f"{b.where}: lint reported {sorted(rules)}")
        errors += quickstart(quick)

    for e in errors:
        print(e, file=sys.stderr)
    total = ", ".join(f"{n} {k}" for k, n in counts.items())
    if errors:
        print(f"guide-check: {len(errors)} problem(s); checked {total}", file=sys.stderr)
        return 1
    note = " (lint and quickstart skipped)" if args.no_lint else ""
    print(f"guide-check: ok, {total}{note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
