#!/usr/bin/env python3
"""Prove every module's property clients from the callees' contracts alone.

Each tests/properties/<module>/props.c was written blind by scripts/blind.py.
A run fails when a manifest is STALE (its hashes no longer match the brief,
props.c or the stripped headers), when a public function is UNCOVERED, when a
client is UNASSERTED (missing, never calls its function, or states nothing),
when a goal is UNMET, or when a smoke goal proves, which makes the client
VACUOUS. Clients are test code proved for "contract implies property", so WP
runs without -wp-rte and no waiver ever applies.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile
from pathlib import Path

import manifest as mf
from tools import ROOT, ToolError, prepend_local_bin, run_framac, which_tool

# Without -wp-rte, WP says once that it generated no RTE guards. That is the
# point of this runner, so it is the one warning accepted; any other fails.
MISSING_RTE = "Missing RTE guards"
VERDICTS = {"none", "valid", "failed", "unknown", "stepout", "timeout"}
OWN_CLAUSE = re.compile(r"(^|_)(ensures|assert)(_|$)")


def static_failures(module: str, root: Path = ROOT) -> list[str]:
    """STALE, UNCOVERED and UNASSERTED lines that need no prover."""
    folder = mf.module_dir(module, root)
    if not (folder / "manifest.toml").is_file():
        return [f"uncovered {module} :: no manifest in {folder.relative_to(root)}"]
    data = mf.load(module, root)
    now = mf.hashes(module, root)
    stale = [f"stale {module} :: {name} changed outside scripts/blind.py"
             for name in ("brief_sha256", "props_sha256") if data.get(name) != now[name]]
    stale += [f"stale {module} :: {path} changed since the manifest was written"
              for path in sorted(set(now["headers"]) | set(data.get("headers", {})))
              if now["headers"].get(path) != data.get("headers", {}).get(path)]
    if stale:
        return stale
    props = (folder / "props.c").read_text()
    stripped = mf.stripped_headers(mf.client_headers(props, root), root)
    declared = mf.declared_functions(mf.strip_header((root / f"include/safec/{module}.h").read_text()))
    entries = data.get("property", [])
    covered = {entry["function"] for entry in entries}
    failures = [f"uncovered {module} :: {name}" for name in declared if name not in covered]
    clients = mf.client_functions(props)
    for entry in entries:
        client = clients.get(entry["client"])
        calls = "|".join(mf.call_names(entry["function"], stripped))
        if client is None:
            reason = "no such function in props.c"
        elif not re.search(rf"\b({calls})\s*\(", client["body"]):
            reason = f"never calls {entry['function']}"
        elif not mf.asserted(client):
            reason = "states nothing beyond \\true"
        else:
            continue
        failures.append(f"unasserted {module} :: {entry['client']} ({entry['id']}: {reason})")
    return failures


def read_report(path: Path, module: str) -> list[dict]:
    try:
        report = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise ToolError(f"error: no WP report for {module}: {exc}") from exc
    if not isinstance(report, list) or not report:
        raise ToolError(f"error: WP reported no goals for {module}")
    for goal in report:
        shape = {"goal": str, "function": str, "passed": bool, "smoke": bool}
        if (not isinstance(goal, dict) or goal.get("verdict") not in VERDICTS
                or not all(isinstance(goal.get(k), t) for k, t in shape.items())):
            raise ToolError(f"error: invalid WP report for {module}")
    return report


def framac() -> str:
    prepend_local_bin()
    found = os.environ.get("FRAMA_C") or which_tool(("frama-c",))
    missing = [name for name in (found, "alt-ergo", "z3") if not name or not which_tool((name,))]
    if missing:
        raise ToolError(f"error: missing required tool: {missing[0] or 'frama-c'} (Frama-C 33.0, "
                        "Alt-Ergo 2.6.4, Z3 4.13.3)")
    return found


def run_wp(cc: str, module: str, root: Path) -> list[dict]:
    source = mf.module_dir(module, root) / "props.c"
    with tempfile.TemporaryDirectory(prefix="safe-c-properties-") as tmp:
        report = Path(tmp) / "report.json"
        command = [cc, "-wp", "-wp-smoke-tests", "-wp-prover", "alt-ergo,z3", "-wp-timeout", "10",
                   "-wp-report-json", str(report), f"-cpp-extra-args=-I{root / 'include'}", str(source)]
        for line in run_framac(command, module).splitlines():
            if "Warning:" in line and line.split("Warning:", 1)[1].strip() != MISSING_RTE:
                raise ToolError(f"error: unsupported proof warning on {module}: {line}")
        return read_report(report, module)


def proof_failures(module: str, report: list[dict], clients: list[str]) -> tuple[int, list[str]]:
    proved, failures, asserting = 0, [], set()
    for goal in report:
        key = f"{module} :: {goal['function']} :: {goal['goal']}"
        if goal["smoke"]:
            if not goal["passed"]:
                failures.append(f"vacuous {key} (smoke goal proved: dead code or inconsistent requires)")
        elif goal["passed"] and goal["verdict"] == "valid":
            proved += 1
            own = goal["goal"].removeprefix(f"typed_{goal['function']}_")
            if not own.startswith("call_") and OWN_CLAUSE.search(own):
                asserting.add(goal["function"])
        else:
            failures.append(f"unmet {key} ({goal['verdict']})")
    failures += [f"unasserted {module} :: {name} (no ensures or assert goal of its own)"
                 for name in clients if name not in asserting and not any(f" :: {name} :: " in f for f in failures)]
    return proved, failures


def check(modules: list[str], root: Path = ROOT) -> int:
    if not (root / mf.PROPERTIES).is_dir():
        raise ToolError(f"error: {mf.PROPERTIES} is missing; every proved module needs a manifest")
    failures = [line for module in modules for line in static_failures(module, root)]
    if failures:
        print("\n".join(failures))
        print(f"properties: {len(failures)} failed before proving")
        return 1
    cc, proved = framac(), 0
    for module in modules:
        clients = [entry["client"] for entry in mf.load(module, root).get("property", [])]
        count, found = proof_failures(module, run_wp(cc, module, root), clients)
        proved += count
        failures += found
    if failures:
        print("\n".join(failures))
    print(f"properties: {proved} goals proved, {len(failures)} failed")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("modules", nargs="*", help="default: every proved module")
    args = parser.parse_args()
    os.chdir(ROOT)
    log = ROOT / "build/properties.log"
    log.parent.mkdir(exist_ok=True)
    with log.open("w") as handle:
        sys.stdout, saved = handle, sys.stdout
        try:
            status = check(args.modules or mf.proved_modules())
        except (ToolError, OSError) as exc:
            print(exc)
            status = 2
        finally:
            sys.stdout = saved
    print(log.read_text(), end="")
    return status


if __name__ == "__main__":
    sys.exit(main())
