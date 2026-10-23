#!/usr/bin/env python3
"""Check the TLAPS proofs in models/*_proofs.tla with the TLA+ proof manager.

Each proof module extends its model (models/channel.tla, models/mailbox.tla) and
proves the model's safety invariants for every size. A proof passes only when
tlapm proves every obligation. tlapm is found at $TLAPM,
build/tools/tlapm/bin/tlapm, or `tlapm` on PATH; scripts/install-tlapm.sh
downloads the pinned build."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from tools import ROOT, ToolError, which_tool

MODELS = ROOT / "models"
OUT = ROOT / "build/tlaps"
# tlapm 1.6.0-pre reports the commit it was built from.
TLAPM_COMMIT = "5f5037f"
PROVED = re.compile(r"All (\d+) obligations? proved")


def find_tlapm() -> str:
    candidates = [os.environ.get("TLAPM"), str(ROOT / "build/tools/tlapm/bin/tlapm"),
                  which_tool(("tlapm",))]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    raise ToolError("error: tlapm not found; run scripts/install-tlapm.sh to download it "
                    "into build/tools/tlapm, or set TLAPM")


def check_version(tlapm: str) -> None:
    result = subprocess.run([tlapm, "--version"], capture_output=True, text=True)
    if result.stdout.strip() != TLAPM_COMMIT:
        raise ToolError(f"error: {tlapm} is not tlapm 1.6.0-pre ({TLAPM_COMMIT}): "
                        f"{(result.stdout + result.stderr).strip()}")


def check(tlapm: str, proof: Path) -> bool:
    # Work on a copy: tlapm writes its .tlacache next to the module.
    work = OUT / proof.stem
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    for spec in MODELS.glob("*.tla"):
        shutil.copy(spec, work / spec.name)
    result = subprocess.run([tlapm, "--stretch", "3", proof.name], cwd=work, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / f"{proof.stem}.log").write_text(result.stdout)
    proved = PROVED.search(result.stdout)
    ok = result.returncode == 0 and proved is not None and "[ERROR]" not in result.stdout
    if ok:
        status = f"pass: all {proved.group(1)} obligations proved"
    else:
        failed = [line.strip() for line in result.stdout.splitlines()
                  if "obligations failed" in line or line.startswith("File ")]
        status = f"FAIL (exit {result.returncode}): {'; '.join(failed[:6]) or 'see log'}"
        print(result.stdout[-4000:])
    print(f"tlaps: {proof.name}: {status}", flush=True)
    return ok


def main() -> int:
    # Optional arguments pick proof modules by name, e.g. mailbox_proofs.
    wanted = sys.argv[1:]
    proofs = [p for p in sorted(MODELS.glob("*_proofs.tla")) if not wanted or p.stem in wanted]
    # Like the other gates, a tree without proofs passes.
    if not proofs:
        print("tlaps: no models/*_proofs.tla")
        return 0
    try:
        tlapm = find_tlapm()
        check_version(tlapm)
    except ToolError as exc:
        print(exc, file=sys.stderr)
        return 1
    # The proofs are independent and tlapm mostly waits on its backends, so run them at once.
    with ThreadPoolExecutor(max_workers=len(proofs)) as pool:
        results = list(pool.map(lambda proof: check(tlapm, proof), proofs))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
