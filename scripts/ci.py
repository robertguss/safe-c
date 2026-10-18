#!/usr/bin/env python3
"""Run every check the Gates workflow runs, in parallel, and print where the time went.

The suites that copy the tree into a temporary directory run side by side. The
steps that write into this tree's build/ (make gates, the self-tests and the
HTTP smoke test) run one after another in a single lane, in the order
listed. Each step's output goes to build/ci/<name>.log.
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

# (name, command, runs in the shared tree, file that must exist for the step to run).
# The shared-tree lane keeps this order; everything else starts longest first.
STEPS = [
    ("gates", ["make", "gates"], True, None),
    ("lint-selftest", ["make", "lint-selftest"], True, None),
    ("gate-selftest", ["make", "gate-selftest"], True, None),
    ("http-smoke", ["make", "-C", "examples/http", "smoke"], True, None),
    ("package-test", ["make", "package-test"], True, "scripts/package-test.sh"),
    ("mutate", ["make", "mutate"], False, "scripts/mutate.py"),
    ("sc09", ["python3", "tests/proofs/sc09_test.py"], False, None),
    ("sc07", ["python3", "tests/proofs/sc07_test.py"], False, None),
    ("sc06", ["python3", "tests/proofs/sc06_test.py"], False, None),
    ("sc05", ["python3", "tests/proofs/sc05_test.py"], False, None),
    ("mutate-test", ["python3", "tests/gates/mutate_test.py"], False, "tests/gates/mutate_test.py"),
    ("model-mutants", ["python3", "tests/models/channel_test.py"], False, None),
    ("runner", ["python3", "tests/gates/runner_test.py"], False, None),
    ("chan", ["python3", "tests/gates/chan_test.py"], False, None),
    ("http-parse", ["python3", "tests/gates/http_parse_test.py"], False, None),
    ("proof-fixtures", ["python3", "tests/gates/proof_test.py"], False, None),
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
        for name, command, shared, _ in steps:
            print(f"{name:15} {'tree ' if shared else 'temp '} {' '.join(command)}")
        return 0
    unknown = set(args.names) - {s[0] for s in steps}
    if unknown:
        parser.error(f"unknown step: {', '.join(sorted(unknown))}")
    if args.names:
        steps = [s for s in steps if s[0] in args.names]

    LOGS.mkdir(parents=True, exist_ok=True)
    lane = [(name, command) for name, command, shared, _ in steps if shared]
    rest = [(name, command) for name, command, shared, _ in steps if not shared]
    started = time.monotonic()
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(run_lane, lane)] if lane else []
        futures += [pool.submit(run_step, name, command) for name, command in rest]
        results = []
        for future in futures:
            outcome = future.result()
            results += outcome if isinstance(outcome, list) else [outcome]
    wall = time.monotonic() - started

    print(f"\nci: {len(results)} steps, {wall:.0f}s wall, jobs={args.jobs}")
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
