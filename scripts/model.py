#!/usr/bin/env python3
"""Model-check models/channel.tla with TLC at each queue size in MODEL_QUEUE_SIZES."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from tools import ROOT, ToolError, which_tool

TLA2TOOLS_VERSION = "1.7.4"
TLA2TOOLS_SHA256 = "936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88"
TLA2TOOLS_URL = (
    f"https://github.com/tlaplus/tlaplus/releases/download/v{TLA2TOOLS_VERSION}/tla2tools.jar"
)
SPEC = ROOT / "models/channel.tla"
CONFIG = ROOT / "models/channel.cfg"
OUT = ROOT / "build/model"
SIZE_LINE = re.compile(r"^(\s*QueueSize\s*=\s*)\d+\s*$", re.MULTILINE)
PASSED = "Model checking completed. No error has been found."


def find_jar() -> Path:
    candidates = [os.environ.get("TLA2TOOLS"), ROOT / "build/tools/tla2tools.jar",
                  Path.home() / ".local/lib/tla2tools.jar"]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            jar = Path(candidate)
            digest = hashlib.sha256(jar.read_bytes()).hexdigest()
            if digest != TLA2TOOLS_SHA256:
                raise ToolError(f"error: {jar} is not tla2tools {TLA2TOOLS_VERSION} "
                                f"(sha256 {digest}, want {TLA2TOOLS_SHA256})")
            return jar
    raise ToolError(f"error: tla2tools.jar not found; download {TLA2TOOLS_URL} to "
                    "build/tools/tla2tools.jar or set TLA2TOOLS")


def queue_sizes() -> list[int]:
    raw = (os.environ.get("MODEL_QUEUE_SIZES") or "1 3").split()
    if not raw or not all(s.isdigit() and int(s) > 0 for s in raw):
        raise ToolError("error: MODEL_QUEUE_SIZES must be positive integers")
    return [int(s) for s in raw]


def check(java: str, jar: Path, size: int) -> bool:
    template = CONFIG.read_text()
    if len(SIZE_LINE.findall(template)) != 1:
        raise ToolError(f"error: {CONFIG.name} needs exactly one 'QueueSize = N' line")
    work = OUT / f"queue-{size}"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    shutil.copy(SPEC, work / SPEC.name)
    (work / CONFIG.name).write_text(SIZE_LINE.sub(rf"\g<1>{size}", template))
    argv = [java, "-XX:+UseParallelGC", "-cp", str(jar), "tlc2.TLC",
            "-workers", "auto", "-cleanup", "-metadir", str(work / "states"),
            "-config", CONFIG.name, SPEC.stem]
    result = subprocess.run(argv, cwd=work, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    (OUT / f"queue-{size}.log").write_text(result.stdout)
    # TLC exits 0 with a warning for some setup problems; require its pass line too.
    ok = result.returncode == 0 and PASSED in result.stdout
    if not ok:
        print(result.stdout)
    summary = [line for line in result.stdout.splitlines()
               if line[:1].isdigit() and "states generated" in line
               or "violated" in line or "Deadlock" in line]
    status = "pass" if ok else f"FAIL (exit {result.returncode})"
    print(f"model: QueueSize={size}: {status}: {'; '.join(summary)}")
    return ok


def main() -> int:
    try:
        java = which_tool(("java",))
        if java is None:
            raise ToolError("error: java not found on PATH (TLC needs Java 11+)")
        jar = find_jar()
        results = [check(java, jar, size) for size in queue_sizes()]
    except ToolError as exc:
        print(exc, file=sys.stderr)
        return 1
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
