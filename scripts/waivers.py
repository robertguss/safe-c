"""Validate and report the explicit proof-gap register."""

from datetime import date
from pathlib import Path
import tomllib

from lint import ROOT, ToolError

FIELDS = ("id", "file", "function", "goal", "reason", "owner", "expires")


def load_waivers(path=ROOT / "waivers.toml"):
    try:
        data = tomllib.loads(Path(path).read_text())
    except (OSError, ValueError) as exc:
        raise ToolError(f"error: cannot read waivers: {exc}") from exc
    waivers = data.get("waiver", [])
    if set(data) - {"waiver"} or not isinstance(waivers, list):
        raise ToolError("error: waivers must use [[waiver]] entries")
    ids, goals = set(), set()
    for waiver in waivers:
        if not isinstance(waiver, dict) or any(not str(waiver.get(k, "")).strip() for k in FIELDS):
            raise ToolError("error: each waiver needs id, file, function, goal, reason, owner, expires")
        if any(not isinstance(waiver[k], str) for k in FIELDS[:-1]):
            raise ToolError("error: waiver fields must be strings")
        file = Path(waiver["file"])
        if file.is_absolute() or ".." in file.parts or file.as_posix() != waiver["file"]:
            raise ToolError(f"error: waiver {waiver['id']} needs a repository-relative file")
        try:
            date.fromisoformat(str(waiver["expires"]))
        except ValueError as exc:
            raise ToolError(f"error: waiver {waiver['id']} has invalid expiry") from exc
        key = (waiver["file"], waiver["function"], waiver["goal"])
        if waiver["id"] in ids or key in goals:
            raise ToolError(f"error: duplicate waiver: {waiver['id']}")
        ids.add(waiver["id"])
        goals.add(key)
    return waivers


def report_waivers(waivers):
    print("id | file | function | goal | reason | owner | expires")
    expired = []
    for waiver in waivers:
        print(" | ".join(str(waiver[k]) for k in FIELDS))
        if date.fromisoformat(str(waiver["expires"])) < date.today():
            expired.append(waiver["id"])
    if expired:
        raise ToolError(f"error: expired waivers: {', '.join(expired)}")
    print(f"waivers: {len(waivers)} active, none expired")
