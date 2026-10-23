#!/usr/bin/env python3
"""Run every check, by hand, in parallel, and print where the time went.

The suites that copy the tree into a temporary directory run side by side. The
steps that write into this tree's build/ (make gates, the self-tests and the
HTTP smoke test) run one after another in a single lane, in the order
listed. The ThreadSanitizer mutant suite runs first, alone. Each step's output
goes to build/ci/<name>.log.
"""

from __future__ import annotations

import argparse
import hashlib
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

from tools import ROOT

LOGS = ROOT / "build/ci"
TAIL = 60

# (name, command, lane, file that must exist for the step to run). "alone" steps run
# first, one at a time, on an idle machine: the ThreadSanitizer mutants of chan and mailbox
# only show their race when the threads really run at once. "tree" steps share one lane in this order;
# "temp" steps start longest first.
STEPS = [
    ("gates", ["make", "gates"], "tree", None),
    ("lint-selftest", ["make", "lint-selftest"], "tree", None),
    ("gate-selftest", ["make", "gate-selftest"], "tree", None),
    ("http-smoke", ["make", "-C", "examples/http", "smoke"], "tree", None),
    ("package-test", ["make", "package-test"], "tree", "scripts/package-test.sh"),
    ("guide", ["make", "guide-test"], "tree", "scripts/guide-check.py"),
    ("mutate", ["make", "mutate"], "temp", "scripts/mutate.py"),
    ("sc09", ["python3", "tests/proofs/sc09_test.py"], "temp", None),
    ("sc07", ["python3", "tests/proofs/sc07_test.py"], "temp", None),
    ("sc06", ["python3", "tests/proofs/sc06_test.py"], "temp", None),
    ("sc05", ["python3", "tests/proofs/sc05_test.py"], "temp", None),
    ("mutate-test", ["python3", "tests/gates/mutate_test.py"], "temp", "tests/gates/mutate_test.py"),
    ("model-mutants", ["python3", "tests/models/channel_test.py"], "temp", None),
    ("mailbox-model-mutants", ["python3", "tests/models/mailbox_test.py"], "temp", None),
    ("tlaps", ["make", "tlaps"], "temp", None),
    ("tlaps-mutants", ["python3", "tests/models/tlaps_test.py"], "temp", None),
    ("genmc", ["make", "genmc"], "temp", None),
    ("genmc-mutants", ["python3", "tests/gates/genmc_test.py"], "temp", None),
    ("runner", ["python3", "tests/gates/runner_test.py"], "temp", None),
    ("lint-verbose", ["python3", "tests/gates/lint_verbose_test.py"], "temp", None),
    ("guide-check", ["python3", "tests/gates/guide_check_test.py"], "temp", None),
    ("chan", ["python3", "tests/gates/chan_test.py"], "alone", None),
    ("mailbox", ["python3", "tests/gates/mailbox_test.py"], "alone", None),
    ("http-parse", ["python3", "tests/gates/http_parse_test.py"], "temp", None),
    ("proof-fixtures", ["python3", "tests/gates/proof_test.py"], "temp", None),
    ("blind", ["python3", "tests/gates/blind_test.py"], "temp", None),
    ("properties-test", ["python3", "tests/gates/properties_test.py"], "temp", None),
]

# Files the workflow uploads under their historical names.
COPIES = {"gates": ROOT / "build/gates.log", "mutate": ROOT / "build/mutate.log"}

_print_lock = threading.Lock()


def say(line: str) -> None:
    with _print_lock:
        print(line, flush=True)


def run_step(name: str, command: list[str]) -> tuple[str, int, float]:
    log = LOGS / f"{name}.log"
    started = time.monotonic()
    say(f"start {name}: {' '.join(command)}")
    with log.open("w") as out:
        code = subprocess.run(command, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT,
                              stdin=subprocess.DEVNULL).returncode
    seconds = time.monotonic() - started
    if name in COPIES:
        COPIES[name].write_bytes(log.read_bytes())
    if code == 0:
        say(f"pass {name} {seconds:.0f}s")
    else:
        tail = log.read_text(errors="replace").splitlines()[-TAIL:]
        say("\n".join([f"FAIL {name} {seconds:.0f}s (exit {code}); last {TAIL} lines of "
                       f"{log.relative_to(ROOT)}:", *("  " + line for line in tail)]))
    return name, code, seconds


def run_lane(steps: list[tuple[str, list[str]]]) -> list[tuple[str, int, float]]:
    return [run_step(name, command) for name, command in steps]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("names", nargs="*", help="run only these steps (default: all)")
    parser.add_argument("-j", "--jobs", type=int,
                        default=int(os.environ.get("CI_JOBS") or os.cpu_count() or 1),
                        help="steps to run at once (default: CI_JOBS or the CPU count)")
    parser.add_argument("--skip", action="append", default=(os.environ.get("CI_SKIP") or "").split(),
                        help="leave out this step (repeatable; CI_SKIP takes a space-separated list)")
    parser.add_argument("--list", action="store_true", help="print the steps and exit")
    parser.add_argument("--image-tag", action="store_true",
                        help="print the CI image tag (a hash of ci/Dockerfile) and exit")
    args = parser.parse_args()
    if args.image_tag:
        print(hashlib.sha256((ROOT / "ci/Dockerfile").read_bytes()).hexdigest()[:12])
        return 0

    steps = [s for s in STEPS if s[3] is None or (ROOT / s[3]).exists()]
    listed = {c[-1] for _, c, _, _ in steps}
    missing = sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / "tests").rglob("*_test.py")
                     if p.relative_to(ROOT).as_posix() not in listed)
    if missing:
        print(f"error: add to STEPS in scripts/ci.py: {', '.join(missing)}", file=sys.stderr)
        return 2
    if args.list:
        for name, command, lane, _ in steps:
            print(f"{name:15} {lane:6} {' '.join(command)}")
        return 0
    unknown = set(args.names + args.skip) - {s[0] for s in STEPS}
    if unknown:
        parser.error(f"unknown step: {', '.join(sorted(unknown))}")
    if args.names:
        steps = [s for s in steps if s[0] in args.names]
    steps = [s for s in steps if s[0] not in args.skip]

    LOGS.mkdir(parents=True, exist_ok=True)
    alone = [(name, command) for name, command, lane, _ in steps if lane == "alone"]
    tree = [(name, command) for name, command, lane, _ in steps if lane == "tree"]
    rest = [(name, command) for name, command, lane, _ in steps if lane == "temp"]
    started = time.monotonic()
    results = run_lane(alone)
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(run_lane, tree)] if tree else []
        futures += [pool.submit(run_step, name, command) for name, command in rest]
        for future in futures:
            outcome = future.result()
            results += outcome if isinstance(outcome, list) else [outcome]
    return report(results, time.monotonic() - started, args.jobs)


def report(results: list[tuple[str, int, float]], wall: float, jobs: int) -> int:
    print(f"\nci: {len(results)} steps, {wall:.0f}s wall, jobs={jobs}")
    for name, code, seconds in sorted(results, key=lambda r: -r[2]):
        print(f"  {'pass' if code == 0 else 'FAIL'} {name:15} {seconds:6.0f}s")
    failed = [name for name, code, _ in results if code != 0]
    if failed:
        print(f"ci: failed: {', '.join(failed)}")
        return 1
    print("ci: all steps passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
