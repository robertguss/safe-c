#!/usr/bin/env python3
"""Model-check stdlib/chan.c and stdlib/mailbox.c with GenMC under the C11 memory model.

Each harness in tests/genmc/ includes the stdlib sources of the tree it runs in,
so GenMC explores every execution RC11 allows of the shipped code at the small
sizes in CONFIGS. An assertion failure or a race on a non-atomic location fails.
GenMC is found at $GENMC, build/tools/genmc/bin/genmc, or `genmc` on PATH;
scripts/install-genmc.sh builds the pinned version."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

from tools import ROOT, ToolError, which_tool

GENMC_VERSION = "0.19.0"
HARNESSES = ROOT / "tests/genmc"
OUT = ROOT / "build/genmc"
PASSED = "No errors were detected."

# (harness, -D settings). Small enough to explore exhaustively in about two minutes.
CONFIGS = [
    ("chan_fifo", "CAP=1 N=3 R=3"),
    ("chan_fifo", "CAP=2 N=3 R=3"),
    ("chan_fifo", "CAP=1 N=4 R=4"),
    ("chan_fifo", "CAP=4 N=3 R=3"),
    ("chan_close", "CAP=1 N=2 R=5"),
    ("chan_close", "CAP=2 N=2 R=5"),
    ("chan_close", "CAP=1 N=3 R=6"),
    ("mbox", "CAP=1 S=2 N=2 R=4"),
    ("mbox", "CAP=2 S=2 N=2 R=4"),
    ("mbox", "CAP=1 S=2 N=2 R=4 CLOSE_AT=0"),
    ("mbox", "CAP=1 S=2 N=2 R=4 CLOSE_AT=1"),
    ("mbox", "CAP=2 S=2 N=2 R=4 CLOSE_AT=2"),
    ("mbox", "CAP=2 S=3 N=1 R=3 CLOSE_AT=1"),
    ("mbox", "CAP=4 S=3 N=1 R=3"),
]


def find_genmc() -> str:
    candidates = [os.environ.get("GENMC"), str(ROOT / "build/tools/genmc/bin/genmc"),
                  which_tool(("genmc",))]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    raise ToolError(f"error: genmc not found; run scripts/install-genmc.sh to build GenMC "
                    f"{GENMC_VERSION} into build/tools/genmc, or set GENMC")


def check_version(genmc: str) -> None:
    result = subprocess.run([genmc, "--version"], capture_output=True, text=True)
    if f"v{GENMC_VERSION}" not in result.stdout + result.stderr:
        raise ToolError(f"error: {genmc} is not GenMC {GENMC_VERSION}: "
                        f"{(result.stdout + result.stderr).strip()}")


def run(genmc: str, harness: str, settings: str) -> bool:
    defines = [f"-D{d}" for d in settings.split()]
    argv = [genmc, "--disable-estimation", "--", f"-I{ROOT}", f"-I{ROOT / 'include'}",
            *defines, str(HARNESSES / f"{harness}.c")]
    result = subprocess.run(argv, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    label = f"{harness}-" + "-".join(d.lower().replace("=", "") for d in settings.split())
    (OUT / f"{label}.log").write_text(result.stdout)
    ok = result.returncode == 0 and PASSED in result.stdout
    executions = re.search(r"complete executions explored: (\d+)", result.stdout)
    errors = [line.strip() for line in result.stdout.splitlines() if line.startswith("Error")]
    if ok:
        status = f"pass: {executions.group(1) if executions else '?'} executions"
    else:
        status = f"FAIL (exit {result.returncode}): {'; '.join(errors) or 'see log'}"
    print(f"genmc: {harness} {settings}: {status}", flush=True)
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--fail-fast", action="store_true", help="stop at the first failure")
    args = parser.parse_args()
    # Like the other gates, a tree without the harnesses passes.
    if not HARNESSES.is_dir():
        print("genmc: no tests/genmc")
        return 0
    try:
        genmc = find_genmc()
        check_version(genmc)
    except ToolError as exc:
        print(exc, file=sys.stderr)
        return 1
    OUT.mkdir(parents=True, exist_ok=True)
    ok = True
    for harness, settings in CONFIGS:
        ok = run(genmc, harness, settings) and ok
        if not ok and args.fail_fast:
            break
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
