#!/usr/bin/env python3
"""Shared repo paths, tool lookup, and the subset compiler warning set."""

from __future__ import annotations

import os
import shlex
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

CLANG_FLAGS = [
    "-std=c11",
    "-Werror",
    "-Wall",
    "-Wextra",
    "-Wvla",
    "-Wconversion",
    "-Wsign-conversion",
]


class ToolError(RuntimeError):
    pass


def which_tool(names: tuple[str, ...]) -> str | None:
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    return None


def prepend_local_bin() -> None:
    local = str(Path.home() / ".local" / "bin")
    path = os.environ.get("PATH", "")
    if path.split(os.pathsep)[:1] != [local]:
        os.environ["PATH"] = local + os.pathsep + path


def repo_rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def run_framac(command: list[str], label: str) -> str:
    """Run Frama-C, echo its transcript, and fail closed on a non-zero exit."""
    print(f"command: {shlex.join(command)}", flush=True)
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(result.stdout, end="", flush=True)
    if result.returncode:
        raise ToolError(f"error: Frama-C failed on {label} (exit {result.returncode})")
    return result.stdout
