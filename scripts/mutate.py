#!/usr/bin/env python3
"""Plant fixed code mutations in each proved file and require WP to reject every one.

A mutant that still proves means the contract does not pin the behavior the
mutation changed. A mutant that does not compile is discarded, not counted as
killed. Survivors that no contract can distinguish go in tests/mutate-equivalent.toml.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import tomllib

from tools import ROOT, ToolError
from prove import default_sources

EQUIVALENT = ROOT / "tests" / "mutate-equivalent.toml"
APP_PROVED = "examples/explorer/sqltok.c"
SWAPS = {"<": "<=", "<=": "<", ">": ">=", ">=": ">", "==": "!=", "!=": "=="}
COMPARE = re.compile(r"(?<![<>=!+\-*/&|^%])(<=|>=|<|>|==|!=)(?![<>=])")
ARITH = re.compile(r"(?<=\s)([+-])(?=\s)")
RETURN = re.compile(r"\breturn (true|false);")


def code_mask(text):
    """Blank out comments (all ACSL lives there), strings, chars and preprocessor lines."""
    out = list(text)
    i, n = 0, len(text)
    line_start = True
    while i < n:
        two = text[i:i + 2]
        if two == "/*" or two == "//":
            end = text.find("*/", i + 2) + 2 if two == "/*" else text.find("\n", i)
            if end < 2:
                end = n
            for j in range(i, end):
                if out[j] != "\n":
                    out[j] = " "
            i = end
        elif text[i] in "\"'":
            j = i + 1
            while j < n and text[j] != text[i]:
                j += 2 if text[j] == "\\" else 1
            for k in range(i, min(j + 1, n)):
                out[k] = " "
            i = j + 1
        elif text[i] == "#" and line_start:
            end = text.find("\n", i)
            end = n if end < 0 else end
            for j in range(i, end):
                out[j] = " "
            i = end
        else:
            if text[i] == "\n":
                line_start = True
            elif not text[i].isspace():
                line_start = False
            i += 1
    return "".join(out)


def match_paren(code, open_at):
    depth = 0
    for i in range(open_at, len(code)):
        depth += code[i] == "("
        depth -= code[i] == ")"
        if depth == 0:
            return i
    return -1


def clauses(code, start, end):
    """Top-level (start, end) spans of a condition split on its outermost || (else &&)."""
    for op in ("||", "&&"):
        depth, last, spans = 0, start, []
        for i in range(start, end):
            depth += code[i] == "("
            depth -= code[i] == ")"
            if depth == 0 and code[i:i + 2] == op:
                spans.append((last, i))
                last = i + 2
        if spans:
            spans.append((last, end))
            return op, spans
    return None, []


def mutants(text):
    """Yield (kind, line, mutated text) for the fixed mutation set."""
    code = code_mask(text)
    line = lambda pos: text.count("\n", 0, pos) + 1
    found = []

    def swap(kind, start, end, new):
        found.append((kind, line(start), text[:start] + new + text[end:]))

    for m in COMPARE.finditer(code):
        swap(f"cmp {m.group(1)}->{SWAPS[m.group(1)]}", m.start(), m.end(), SWAPS[m.group(1)])
    for m in ARITH.finditer(code):
        swap(f"arith {m.group(1)}", m.start(), m.end(), "-" if m.group(1) == "+" else "+")
    for m in RETURN.finditer(code):
        swap(f"return {m.group(1)}", m.start(), m.end(), f"return {'false' if m.group(1) == 'true' else 'true'};")
    for m in re.finditer(r"\bif\s*\(", code):
        open_at = m.end() - 1
        close = match_paren(code, open_at)
        if close < 0:
            continue
        swap("if-cond->0", open_at + 1, close, "0")
        op, spans = clauses(code, open_at + 1, close)
        for a, b in spans:
            swap("drop-clause", a, b, "0" if op == "||" else "1")
    seen, unique, nth = set(), [], {}
    for kind, ln, mutated in found:
        if mutated != text and mutated not in seen:
            seen.add(mutated)
            # The nth mutant of a kind on a line keeps equivalent-list keys unambiguous.
            nth[(ln, kind)] = nth.get((ln, kind), 0) + 1
            unique.append((f"{kind} #{nth[(ln, kind)]}", ln, mutated))
    return unique


def functions(text):
    code = code_mask(text)
    found, depth, head, start = [], 0, 0, None
    for i, c in enumerate(code):
        if c == "{":
            if depth == 0:
                m = re.search(r"\b(\w+)\s*\([^;{}=]*\)\s*$", code[head:i])
                start = (i, m.group(1)) if m else None
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                if start:
                    found.append((text.count("\n", 0, start[0]) + 1, text.count("\n", 0, i) + 1, start[1]))
                head, start = i + 1, None
        elif c == ";" and depth == 0:
            head = i + 1
    return found


def run_prove(tree, source, waivers, function=None):
    command = [sys.executable, str(tree / "scripts" / "prove.py"), "--waivers", str(waivers), source]
    if function:
        command[2:2] = ["--function", function]
    result = subprocess.run(command, cwd=tree, capture_output=True, text=True, timeout=1800)
    return result.returncode, result.stdout + result.stderr


def classify(status, output):
    if status == 0:
        return "survived"
    if status == 1 or "unsupported proof warning" in output:
        return "killed"
    return "invalid"


def load_equivalent(path):
    if not path.exists():
        return set()
    entries = tomllib.loads(path.read_text()).get("equivalent", [])
    keys = set()
    for e in entries:
        if not all(isinstance(e.get(k), str) and e[k] for k in ("file", "line", "kind", "reason")):
            raise ToolError(f"error: malformed entry in {path}")
        keys.add((e["file"], e["line"], e["kind"]))
    return keys


def workspace(base, source, waivers):
    for name in ("scripts", "include"):
        shutil.copytree(ROOT / name, base / name, dirs_exist_ok=True)
    (base / "build").mkdir(exist_ok=True)
    dest = base / source
    dest.parent.mkdir(parents=True, exist_ok=True)
    if Path(source).parent != Path("stdlib"):
        for header in (ROOT / source).parent.glob("*.h"):
            shutil.copy(header, dest.parent / header.name)
    shutil.copy(ROOT / source, dest)
    shutil.copy(waivers, base / "waivers.toml")
    return base / "waivers.toml"


def mutate_file(source, waivers, jobs, equivalent, keep=None):
    original = (ROOT / source).read_text()
    plan = [m for i, m in enumerate(mutants(original)) if keep is None or i in keep]
    bodies = functions(original)
    if keep is not None and not keep:
        # Another shard owns every mutant of this file, and its baseline.
        return {"killed": 0, "survived": 0, "invalid": 0, "equivalent": 0}
    with tempfile.TemporaryDirectory(prefix="safe-c-mutate-") as tmp:
        tmp = Path(tmp)
        base = tmp / "baseline"
        waived = workspace(base, source, waivers)
        status, output = run_prove(base, source, waived)
        if status != 0:
            raise ToolError(f"error: baseline {source} does not prove; fix it before mutating\n{output}")

        def one(item):
            index, (kind, ln, mutated) = item
            tree = tmp / f"m{index}"
            waived = workspace(tree, source, waivers)
            (tree / source).write_text(mutated)
            function = next((name for first, last, name in bodies if first <= ln <= last), None)
            return kind, ln, classify(*run_prove(tree, source, waived, function))

        with ThreadPoolExecutor(max_workers=jobs) as pool:
            results = list(pool.map(one, enumerate(plan)))
    counts = {"killed": 0, "survived": 0, "invalid": 0, "equivalent": 0}
    for kind, ln, verdict in results:
        key = (source, str(ln), kind)
        if verdict == "survived" and key in equivalent:
            verdict = "equivalent"
        counts[verdict] += 1
        if verdict in ("survived", "invalid"):
            print(f"{verdict} {source}:{ln} [{kind}]")
    print(f"mutate {source}: {counts['killed']} killed, {counts['survived']} survived, "
          f"{counts['equivalent']} equivalent, {counts['invalid']} discarded (do not compile)")
    return counts


def parse_shard(value):
    """'K/N' -> (K, N) with 1 <= K <= N; None when unset."""
    if not value:
        return None
    try:
        k, n = (int(x) for x in value.split("/"))
    except ValueError:
        raise argparse.ArgumentTypeError(f"shard must be K/N, got {value!r}")
    if not 1 <= k <= n:
        raise argparse.ArgumentTypeError(f"shard must be K/N with 1 <= K <= N, got {value!r}")
    return k, n


def shard_plan(sources, shard):
    """Map each source to the mutant indexes this shard runs (None: all of them).

    Mutants are dealt round-robin across the whole run, so shards stay even
    however the files differ in size."""
    if shard is None:
        return {source: None for source in sources}
    k, n = shard
    keep, seen = {}, 0
    for source in sources:
        count = len(mutants((ROOT / source).read_text()))
        # A file with no mutants still needs its baseline proved; shard 1 does it.
        keep[source] = (None if count == 0 and k == 1 else
                        {i for i in range(count) if (seen + i) % n == k - 1})
        seen += count
    return keep


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--waivers", type=Path, default=ROOT / "waivers.toml")
    parser.add_argument("--jobs", type=int, default=int(os.environ.get("MUTATE_JOBS") or os.cpu_count() or 1))
    parser.add_argument("--list", action="store_true", help="print the planned mutants without running WP")
    parser.add_argument("--shard", type=parse_shard, default=os.environ.get("MUTATE_SHARD") or None,
                        help="run only shard K of N (K/N), e.g. one per CI runner")
    parser.add_argument("sources", nargs="*", type=Path)
    args = parser.parse_args()
    os.chdir(ROOT)
    sources = [p.as_posix() for p in (args.sources or default_sources() + [Path(APP_PROVED)])]
    keep = shard_plan(sources, args.shard)
    if args.list:
        for source in sources:
            for i, (kind, ln, _) in enumerate(mutants((ROOT / source).read_text())):
                if keep[source] is None or i in keep[source]:
                    print(f"{source}:{ln} [{kind}]")
        return 0
    started = time.monotonic()
    equivalent = load_equivalent(EQUIVALENT)
    survived = total = 0
    try:
        for source in sources:
            counts = mutate_file(source, args.waivers.resolve(), max(args.jobs, 1), equivalent,
                                 keep[source])
            survived += counts["survived"]
            total += sum(counts.values()) - counts["invalid"]
    except (ToolError, OSError, subprocess.TimeoutExpired) as exc:
        print(exc)
        return 2
    print(f"mutate: {total} mutants, {survived} survived, {time.monotonic() - started:.0f}s")
    return 1 if survived else 0


if __name__ == "__main__":
    sys.exit(main())
