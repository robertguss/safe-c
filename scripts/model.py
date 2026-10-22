#!/usr/bin/env python3
"""Model-check models/channel.tla and models/mailbox.tla with TLC.

Each queue size in MODEL_QUEUE_SIZES runs for the channel; each sender count in
MODEL_SENDERS (default 2 and 3) at each queue size runs for the mailbox."""

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
MODELS = ROOT / "models"
OUT = ROOT / "build/model"
SIZE_LINE = re.compile(r"^(\s*QueueSize\s*=\s*)\d+\s*$", re.MULTILINE)
SENDERS_LINE = re.compile(r"^(\s*Senders\s*=\s*)\{[^}]*\}\s*$", re.MULTILINE)
PASSED = "Model checking completed. No error has been found."


def find_jar() -> Path:
    candidates = [os.environ.get("TLA2TOOLS"), ROOT / "build/tools/tla2tools.jar"]
    try:
        candidates.append(Path.home() / ".local/lib/tla2tools.jar")
    except RuntimeError:
        pass  # no home directory, as under `docker run --user`
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


def sender_counts() -> list[int]:
    raw = (os.environ.get("MODEL_SENDERS") or "2 3").split()
    if not raw or not all(s.isdigit() and int(s) > 0 for s in raw):
        raise ToolError("error: MODEL_SENDERS must be positive integers")
    return [int(s) for s in raw]


def check(java: str, jar: Path, name: str, size: int, senders: int | None = None) -> bool:
    spec = MODELS / f"{name}.tla"
    config = MODELS / f"{name}.cfg"
    template = config.read_text()
    if len(SIZE_LINE.findall(template)) != 1:
        raise ToolError(f"error: {config.name} needs exactly one 'QueueSize = N' line")
    label = f"queue-{size}" if name == "channel" else f"{name}-senders-{senders}-queue-{size}"
    work = OUT / label
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    shutil.copy(spec, work / spec.name)
    text = SIZE_LINE.sub(rf"\g<1>{size}", template)
    if senders is not None:
        if len(SENDERS_LINE.findall(text)) != 1:
            raise ToolError(f"error: {config.name} needs exactly one 'Senders = {{...}}' line")
        names = ", ".join(f"s{i}" for i in range(1, senders + 1))
        text = SENDERS_LINE.sub(lambda m: f"{m.group(1)}{{{names}}}", text)
    (work / config.name).write_text(text)
    # TLC unpacks its standard modules (Naturals.tla ...) into java.io.tmpdir; runs that
    # share /tmp (gates beside the mutant suites) can parse a half-written copy.
    (work / "tmp").mkdir()
    argv = [java, "-XX:+UseParallelGC", f"-Djava.io.tmpdir={work / 'tmp'}", "-cp", str(jar), "tlc2.TLC",
            "-workers", "auto", "-cleanup", "-metadir", str(work / "states"),
            "-config", config.name, spec.stem]
    result = subprocess.run(argv, cwd=work, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    (OUT / f"{label}.log").write_text(result.stdout)
    # TLC exits 0 with a warning for some setup problems; require its pass line too.
    ok = result.returncode == 0 and PASSED in result.stdout
    if not ok:
        print(result.stdout)
    summary = [line for line in result.stdout.splitlines()
               if line[:1].isdigit() and "states generated" in line
               or "violated" in line or "Deadlock" in line]
    status = "pass" if ok else f"FAIL (exit {result.returncode})"
    prefix = f"QueueSize={size}" if senders is None else f"{name}: Senders={senders}, QueueSize={size}"
    print(f"model: {prefix}: {status}: {'; '.join(summary)}")
    return ok


def main() -> int:
    # Like the other gates, a tree without the model passes; jar and Java are not needed.
    if not (MODELS / "channel.tla").is_file():
        print("model: no models/channel.tla")
        return 0
    try:
        java = which_tool(("java",))
        if java is None:
            raise ToolError("error: java not found on PATH (TLC needs Java 11+)")
        jar = find_jar()
        sizes = queue_sizes()
        results = [check(java, jar, "channel", size) for size in sizes]
        if (MODELS / "mailbox.tla").is_file():
            results += [check(java, jar, "mailbox", size, n)
                        for n in sender_counts() for size in sizes]
    except ToolError as exc:
        print(exc, file=sys.stderr)
        return 1
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
