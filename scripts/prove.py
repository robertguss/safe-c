#!/usr/bin/env python3
"""Run Frama-C WP and accept only proved goals or exact, unexpired waivers."""

import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from tools import ROOT, ToolError, prepend_local_bin, repo_rel, which_tool
from waivers import load_waivers, report_waivers

# Frama-C 33 announces these omitted guard classes even on scalar-only fixtures.
RTE_GAPS = {
    "unaligned pointers (\\aligned not supported)": "wp-rte-unaligned",
    "invalid function pointer calls (\\valid_function not supported)": "wp-rte-function-pointer",
}


# Threads and atomics are outside WP's model; stdlib/chan.c and stdlib/mailbox.c are
# checked by tests, ThreadSanitizer (make tsan) and models/channel.tla or
# models/mailbox.tla instead.
UNPROVED_MODULES = {"stdlib/chan.c", "stdlib/mailbox.c"}


def default_sources():
    return [p for p in sorted(Path("stdlib").glob("*.c")) if p.as_posix() not in UNPROVED_MODULES]


def check_report(report, source):
    if not isinstance(report, list) or not report:
        raise ToolError(f"error: no WP goals reported for {source}")
    for goal in report:
        if (not isinstance(goal, dict) or not isinstance(goal.get("goal"), str)
                or not isinstance(goal.get("file"), str)
                or not isinstance(goal.get("passed"), bool)
                or goal.get("smoke") is not False
                or goal.get("verdict") not in {"none", "valid", "failed", "unknown", "stepout", "timeout"}):
            raise ToolError(f"error: invalid WP report for {source}")
    return report


def prove(sources, waivers_path):
    prepend_local_bin()
    waivers = load_waivers(waivers_path)
    report_waivers(waivers)
    cc = os.environ.get("FRAMA_C") or which_tool(("frama-c",))
    if not cc or not which_tool((cc,)):
        raise ToolError("error: missing required tool: frama-c (install Frama-C 33.0)")
    for prover in ("alt-ergo", "z3"):
        if not which_tool((prover,)):
            raise ToolError(f"error: missing required tool: {prover}")
    if not sources:
        print("prove: no stdlib/*.c sources")
        return 0
    approved = {(w["file"], w["function"], w["goal"]): w["id"] for w in waivers}
    proved = waived = failed = 0
    with tempfile.TemporaryDirectory(prefix="safe-c-wp-") as tmp:
        for index, source in enumerate(sources):
            report_path = Path(tmp) / f"{index}.json"
            command = [cc, "-wp", "-wp-rte", "-warn-unsigned-overflow",
                       "-wp-prover", "alt-ergo,z3", "-wp-timeout", "10",
                       "-wp-report-json", str(report_path), "-cpp-extra-args=-Iinclude", str(source)]
            print(f"command: {shlex.join(command)}", flush=True)
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            print(result.stdout, end="", flush=True)
            if result.returncode:
                raise ToolError(f"error: Frama-C failed on {source} (exit {result.returncode})")
            report = check_report(json.loads(report_path.read_text()), source)
            gaps = {}
            for line in result.stdout.splitlines():
                if "Warning:" not in line:
                    continue
                message = line.split("Warning:", 1)[1].strip()
                omitted = message.removeprefix("Skipped RTE guards: ")
                if message.startswith("Skipped RTE guards: ") and omitted in RTE_GAPS:
                    gaps[RTE_GAPS[omitted]] = {"goal": RTE_GAPS[omitted], "file": str(source),
                                 "function": "<translation-unit>", "passed": False, "verdict": "unknown"}
                else:
                    raise ToolError(f"error: unsupported proof warning on {source}: {line}")
            report.extend(gaps.values())
            for goal in report:
                if goal["passed"] and goal["verdict"] == "valid":
                    proved += 1
                    continue
                key = (repo_rel(Path(goal["file"])), goal.get("function", "<translation-unit>"), goal["goal"])
                if key in approved:
                    waived += 1
                    print(f"waived {approved[key]}: {' :: '.join(key)}")
                else:
                    failed += 1
                    print(f"unproved {' :: '.join(key)} ({goal['verdict']})")
    print(f"prove: {proved} proved, {waived} waived, {failed} unproved")
    return 1 if failed else 0


def selftest():
    source = Path("tests/gates/wp-canary.c")
    with tempfile.TemporaryDirectory(prefix="safe-c-wp-selftest-") as tmp:
        waivers = Path(tmp) / "waivers.toml"
        entries = []
        for goal in (*RTE_GAPS.values(), "typed_wp_canary_ensures_canary"):
            function = "<translation-unit>" if goal in RTE_GAPS.values() else "wp_canary"
            entries.append(f'''[[waiver]]
id = "selftest-{goal}"
file = "{source}"
function = "{function}"
goal = "{goal}"
reason = "Deliberate gate fixture; never a production waiver"
owner = "Safe C self-test"
expires = "9999-12-31"
''')
        # Waive only tool-model omissions first, so they cannot mask a weakened canary.
        waivers.write_text("\n".join(entries[:-1]))
        if prove([source], waivers) != 1:
            raise ToolError("error: WP canary was not caught")
        waivers.write_text("\n".join(entries))
        if prove([source], waivers) != 0:
            raise ToolError("error: matching WP waiver did not pass")
        waivers.write_text(waivers.read_text().replace("9999-12-31", "2000-01-01"))
        try:
            report_waivers(load_waivers(waivers))
        except ToolError as exc:
            if "expired" not in str(exc):
                raise
        else:
            raise ToolError("error: expired WP waiver was accepted")
    print("wp canary caught; matching waiver accepted; expired waiver rejected")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--waivers", type=Path, default=ROOT / "waivers.toml")
    parser.add_argument("sources", nargs="*", type=Path)
    args = parser.parse_args()
    os.chdir(ROOT)
    log = ROOT / "build" / ("prove-selftest.log" if args.selftest else "prove.log")
    log.parent.mkdir(exist_ok=True)
    # Keep the complete transcript even when schema/tool errors abort the gate.
    with log.open("w") as handle:
        old_stdout = sys.stdout
        sys.stdout = handle
        try:
            status = selftest() if args.selftest else prove(args.sources or default_sources(), args.waivers)
        except (ToolError, OSError, ValueError) as exc:
            print(exc)
            status = 2
        finally:
            sys.stdout = old_stdout
    print(log.read_text(), end="")
    return status


if __name__ == "__main__":
    sys.exit(main())
