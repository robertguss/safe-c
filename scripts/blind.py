#!/usr/bin/env python3
"""Write a module's property manifest with a model that sees only the brief and stripped headers.

The only writer of tests/properties/<module>/. It never runs in a gate:

  author MODULE   first manifest from brief.md
  update MODULE   new or changed declarations: the author revises its own manifest
  dispute MODULE  rerun with one property and a plain-English objection (R11)

Codex runs first, through the Codex plugin's companion script, read-only, in an
empty temporary directory. When the companion is missing, not ready or fails,
`claude -p` runs a different Claude model there with no tools. Each property is
stamped with the provider and model of the run that last wrote it.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

import manifest as mf
from tools import ROOT, ToolError, which_tool

PROMPT = Path(__file__).with_name("blind-prompt.md")
COMPANION = ".claude/plugins/cache/openai-codex/codex/*/scripts/codex-companion.mjs"
FENCE = re.compile(r"^```(\w*)[ \t]*\n(.*?)^```[ \t]*$", re.M | re.S)
TASKS = {
    "author": "Write the property manifest for the `{module}` module.",
    "update": "The `{module}` headers changed since you wrote its manifest. Revise it: add properties "
              "for functions that have none, fix clients whose declarations changed, keep the rest.",
    "dispute": "The code author objects to property `{property}` of the `{module}` module. Decide on "
               "the merits, from the brief alone: keep it unchanged, rewrite it, or remove it.",
}
DISPUTE_HEAD = 'decision = "keep"  # keep, rewrite or remove\nreason = "why, in one or two sentences"\n\n'


def codex_command() -> list[str] | None:
    if os.environ.get("BLIND_CODEX"):
        return shlex.split(os.environ["BLIND_CODEX"])
    found = sorted(Path.home().glob(COMPANION))
    return ["node", str(found[-1])] if found and which_tool(("node",)) else None


def codex_model() -> str:
    try:
        model = tomllib.loads((Path.home() / ".codex/config.toml").read_text()).get("model")
    except (OSError, tomllib.TOMLDecodeError):
        model = None
    if model:
        return model
    version = subprocess.run(["codex", "--version"], capture_output=True, text=True) if which_tool(("codex",)) else None
    return f"default ({version.stdout.strip()})" if version else "default"


def run_codex(work: Path, state: Path) -> tuple[str | None, dict]:
    """Codex's reply and run record, or (None, record with the fallback reason)."""
    command = codex_command()
    if not command:
        return None, {"fallback_reason": "codex companion not found"}
    env = {**os.environ, "CLAUDE_PLUGIN_DATA": str(state)}
    setup = subprocess.run([*command, "setup", "--json"], capture_output=True, text=True, env=env)
    try:
        ready = setup.returncode == 0 and json.loads(setup.stdout).get("ready") is True
    except json.JSONDecodeError:
        ready = False
    if not ready:
        return None, {"fallback_reason": "codex companion not ready"}
    task = subprocess.run([*command, "task", "--fresh", "--json", "--cwd", str(work),
                           "--prompt-file", "prompt.md"], capture_output=True, text=True, env=env, cwd=work)
    if task.returncode:
        return None, {"fallback_reason": f"codex task exited {task.returncode}"}
    logs = sorted(state.glob("**/jobs/*.log"))
    record = {"provider": "openai-codex", "model": codex_model(),
              "job_id": logs[-1].stem if logs else "",
              "log": task.stdout + "".join(p.read_text() for p in logs)}
    try:
        return json.loads(task.stdout).get("rawOutput", ""), record
    except json.JSONDecodeError:
        return task.stdout, record


def run_claude(work: Path, prompt: str, model: str) -> tuple[str, dict]:
    claude = os.environ.get("BLIND_CLAUDE") or which_tool(("claude",))
    if not claude:
        raise ToolError("error: codex is unavailable and claude is not on PATH")
    command = [claude, "-p", "--model", model, "--tools", "", "--strict-mcp-config",
               "--no-session-persistence"]
    result = subprocess.run(command, input=prompt, capture_output=True, text=True, cwd=work)
    if result.returncode:
        raise ToolError(f"error: fallback claude exited {result.returncode}: {result.stderr.strip()}")
    return result.stdout, {"provider": "anthropic-claude", "model": model}


def run_model(files: dict[str, str], fallback: str) -> tuple[str, dict]:
    """Run the author in a fresh directory outside the repo that holds only `files`."""
    with tempfile.TemporaryDirectory(prefix="safe-c-blind-") as tmp:
        work, state = Path(tmp) / "work", Path(tmp) / "codex-state"
        for name, text in files.items():
            (work / name).parent.mkdir(parents=True, exist_ok=True)
            (work / name).write_text(text)
        state.mkdir()
        reply, record = run_codex(work, state)
        if reply is None:
            reason = record["fallback_reason"]
            reply, record = run_claude(work, files["prompt.md"], fallback)
            record["fallback_reason"] = reason
    return reply, record


def fenced(reply: str, lang: str) -> str | None:
    blocks = [body for tag, body in FENCE.findall(reply) if tag.lower() == lang]
    return blocks[-1] if blocks else None


def parse_reply(reply: str, command: str) -> dict:
    head = fenced(reply, "toml")
    if head is None:
        raise ToolError("error: reply has no fenced toml block")
    try:
        parsed = tomllib.loads(head)
    except tomllib.TOMLDecodeError as exc:
        raise ToolError(f"error: reply toml does not parse: {exc}") from exc
    parsed["props"] = fenced(reply, "c")
    if command == "dispute" and parsed.get("decision") not in {"keep", "rewrite", "remove"}:
        raise ToolError("error: dispute reply needs decision = keep, rewrite or remove")
    if parsed.get("decision") == "keep":
        return parsed
    if parsed["props"] is None:
        raise ToolError("error: reply has no fenced c block")
    entries = parsed.get("property")
    fields = ("id", "function", "statement", "client")
    if not entries or not all(isinstance(e.get(f), str) and e[f] for e in entries for f in fields):
        raise ToolError("error: every [[property]] needs id, function, statement and client")
    if len({e["id"] for e in entries}) != len(entries):
        raise ToolError("error: property ids repeat")
    return parsed


def stamp(entries: list[dict], props: str, old: dict, old_props: str, author: str) -> list[dict]:
    """Keep the old author on entries this run left exactly as they were (R6)."""
    before = {e["id"]: e for e in old.get("property", [])}
    old_clients, new_clients = mf.client_functions(old_props), mf.client_functions(props)
    out = []
    for entry in entries:
        fields = {k: entry[k] for k in ("id", "function", "statement", "client")}
        prior = before.get(entry["id"])
        same = (prior and all(prior.get(k) == v for k, v in fields.items())
                and old_clients.get(entry["client"]) == new_clients.get(entry["client"]))
        out.append({**fields, "author": prior["author"] if same else author})
    return out


def header_section(stripped: dict[str, str]) -> str:
    return "\n\n".join(f"### {path.removeprefix('include/')}\n\n```c\n{text}```" for path, text in stripped.items())


def gather(args, old: dict, old_props: str) -> tuple[dict[str, str], str]:
    """The files the author sees, and the prompt built from them."""
    folder = mf.module_dir(args.module)
    names = [f"safec/{args.module}.h"] + mf.LOCAL_INCLUDE.findall(old_props)
    stripped = mf.stripped_headers(mf.header_closure(names))
    files = {"brief.md": (folder / "brief.md").read_text()}
    files.update({path: text for path, text in stripped.items()})
    extra = ""
    if args.command != "author":
        listing = mf.dump({"property": [{k: e[k] for k in ("id", "function", "statement", "client")}
                                        for e in old.get("property", [])]})
        files.update({"props.c": old_props, "properties.toml": listing})
        extra = f"## Your current manifest\n\n```toml\n{listing}```\n\n```c\n{old_props}```\n"
    if args.command == "dispute":
        files["objection.md"] = args.objection.read_text()
        extra += f"\n## The objection to `{args.property}`\n\n{files['objection.md']}\n"
    prompt = (PROMPT.read_text().replace("{{task}}", TASKS[args.command].format(**vars(args)))
              .replace("{{brief}}", files["brief.md"]).replace("{{headers}}", header_section(stripped))
              .replace("{{extra}}", extra)
              .replace("{{toml_head}}", DISPUTE_HEAD if args.command == "dispute" else ""))
    files["prompt.md"] = prompt
    return files, prompt


def write(args, parsed: dict, record: dict, old: dict, old_props: str) -> None:
    folder = mf.module_dir(args.module)
    author = f"{record['provider']}:{record['model']}"
    today = datetime.date.today().isoformat()
    props = old_props if parsed.get("decision") == "keep" else parsed["props"]
    entries = old.get("property", []) if parsed.get("decision") == "keep" else stamp(
        parsed["property"], props, old, old_props, author)
    if parsed.get("decision") == "remove" and any(e["id"] == args.property for e in entries):
        raise ToolError(f"error: decision is remove but {args.property} is still listed")
    (folder / "props.c").write_text(props)
    run = {"command": args.command, "provider": record["provider"], "model": record["model"], "date": today}
    run.update({k: record[k] for k in ("fallback_reason", "job_id") if record.get(k)})
    disputes = old.get("dispute", [])
    if args.command == "dispute":
        disputes = disputes + [{"property": args.property, "objection": args.objection.read_text(),
                                "decision": parsed["decision"], "reason": str(parsed.get("reason", "")),
                                "author": author, "date": today}]
    sources = old.get("source", []) or [dict(zip(("card", "page", "edited"), re.split(r"[=@]", s, maxsplit=2)))
                                        for s in args.source]
    data = {"module": args.module, **mf.hashes(args.module), "source": sources,
            "run": old.get("run", []) + [run], "property": entries, "dispute": disputes}
    (folder / "manifest.toml").write_text(mf.dump({k: v for k, v in data.items() if v != []}))


def blind(args) -> int:
    if args.fallback_model.lower() == args.code_model.lower():
        raise ToolError(f"error: the fallback model {args.fallback_model} is the code agent's model; "
                        "pass a different --fallback-model")
    folder = mf.module_dir(args.module)
    if not (folder / "brief.md").is_file():
        raise ToolError(f"error: {folder.relative_to(ROOT)}/brief.md is missing")
    old = mf.load(args.module) if args.command != "author" else {}
    old_props = (folder / "props.c").read_text() if args.command != "author" else ""
    if args.command == "dispute" and args.property not in {e["id"] for e in old.get("property", [])}:
        raise ToolError(f"error: no property {args.property} in {args.module}")
    files, _ = gather(args, old, old_props)
    reply, record = run_model(files, args.fallback_model)
    (folder / "reply.txt").write_text(reply)
    log = record.pop("log", "")
    if log:
        (folder / "codex.log").write_text(log)
    # macOS reaches /var through the /private symlink; a log may use either spelling.
    if any(path in log for path in {str(ROOT), str(ROOT).removeprefix("/private")}):
        raise ToolError("error: the codex log mentions the repo path, so the run was not blind")
    write(args, parse_reply(reply, args.command), record, old, old_props)
    print(f"blind: wrote {folder.relative_to(ROOT)} with {record['provider']}:{record['model']}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=sorted(TASKS))
    parser.add_argument("module", choices=mf.proved_modules())
    parser.add_argument("--code-model", required=True, help="the code agent's model alias, e.g. opus")
    parser.add_argument("--fallback-model", default="sonnet")
    parser.add_argument("--source", action="append", default=[], help="CARD=NOTION_PAGE_ID@LAST_EDITED")
    parser.add_argument("--property", help="dispute: the property id")
    parser.add_argument("--objection", type=Path, help="dispute: file holding the objection")
    args = parser.parse_args()
    if args.command == "dispute" and not (args.property and args.objection):
        parser.error("dispute needs --property and --objection")
    try:
        return blind(args)
    except (ToolError, OSError) as exc:
        print(exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
