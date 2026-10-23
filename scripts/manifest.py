#!/usr/bin/env python3
"""The property manifest format, shared by scripts/blind.py (the only writer) and
scripts/properties.py (the gate).

Each proved module has tests/properties/<module>/ holding brief.md, props.c,
manifest.toml and the raw model reply. The blind author sees the public headers
stripped of every function contract and comment (strip_header); the manifest
records hashes of the brief, of props.c and of each stripped header props.c
includes, so a hand edit or a changed declaration or predicate goes STALE.
"""

from __future__ import annotations

import hashlib
import json
import re
import tomllib
from pathlib import Path

from tools import ROOT, ToolError

PROPERTIES = Path("tests/properties")
INCLUDE = Path("include")
# Threads and atomics are outside WP (scripts/prove.py), so they have no manifest.
UNPROVED = {"chan", "mailbox"}

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
LOCAL_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
DECLARATION = re.compile(r"^[A-Za-z_][\w \t*]*?\b(\w+)\s*\([^;{}]*\)\s*;", re.M)
DEFINITION = re.compile(r"^[A-Za-z_][\w \t*]*?\b(\w+)\s*\([^;{}]*\)\s*\{", re.M)
MACRO = re.compile(r"^#\s*define\s+(\w+)\(.*", re.M)
CLAUSE = re.compile(r"\b(ensures|assert)\b(.*?);", re.S)


def proved_modules(root: Path = ROOT) -> list[str]:
    return sorted(p.stem for p in (root / "stdlib").glob("*.c") if p.stem not in UNPROVED)


def sha256(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def strip_header(text: str) -> str:
    """Drop every comment and every ACSL block except predicate and logic definitions."""
    def keep(match: re.Match) -> str:
        body = match.group(0)
        if body.startswith("/*@") and re.match(r"\s*(predicate|logic)\b", body[3:]):
            return body
        return ""
    stripped = COMMENT.sub(keep, text)
    lines = [line.rstrip() for line in stripped.splitlines()]
    return re.sub(r"\n{3,}", "\n\n", "\n".join(lines)).strip() + "\n"


def header_closure(names: list[str], root: Path = ROOT) -> list[str]:
    """Every public header the named ones pull in, as paths like include/safec/slice.h."""
    seen: list[str] = []
    pending = list(names)
    while pending:
        rel = pending.pop(0)
        if rel in seen:
            continue
        path = root / INCLUDE / rel
        if not rel.startswith("safec/") or not path.is_file():
            raise ToolError(f"error: not a public header: {rel}")
        seen.append(rel)
        pending += [f"safec/{name}" for name in LOCAL_INCLUDE.findall(path.read_text())]
    return [f"{INCLUDE}/{rel}" for rel in seen]


def client_headers(props: str, root: Path = ROOT) -> list[str]:
    return header_closure(LOCAL_INCLUDE.findall(props), root)


def stripped_headers(paths: list[str], root: Path = ROOT) -> dict[str, str]:
    return {path: strip_header((root / path).read_text()) for path in paths}


def declared_functions(stripped: str) -> list[str]:
    code = re.sub(r"^#.*?(?<!\\)$", "", stripped, flags=re.M | re.S)
    return DECLARATION.findall(code)


def call_names(function: str, stripped: dict[str, str]) -> list[str]:
    """The function plus every header macro that expands to a call of it."""
    names = [function]
    for text in stripped.values():
        for line in re.findall(r"^#\s*define\b.*?(?<!\\)$", text, re.M | re.S):
            match = MACRO.match(line)
            if match and re.search(rf"\b{function}\s*\(", line):
                names.append(match.group(1))
    return names


def client_functions(props: str) -> dict[str, dict[str, str]]:
    """Each function defined in props.c: its contract (the ACSL block just before it) and body."""
    found = {}
    for match in DEFINITION.finditer(props):
        depth, end = 0, match.end() - 1
        for end in range(match.end() - 1, len(props)):
            depth += {"{": 1, "}": -1}.get(props[end], 0)
            if depth == 0:
                break
        before = props[:match.start()].rstrip()
        contract = ""
        if before.endswith("*/"):
            start = before.rfind("/*")
            if before.startswith("/*@", start):
                contract = before[start:]
        found[match.group(1)] = {"contract": contract, "body": props[match.end():end],
                                 "text": contract + "\n" + props[match.start():end + 1]}
    return found


def asserted(client: dict[str, str]) -> bool:
    """True when the client states something: an ensures or assert clause other than \\true."""
    clauses = CLAUSE.findall(client["contract"]) + CLAUSE.findall(client["body"])
    return any(expr.strip().removeprefix(":").strip() not in {"\\true", ""} for _, expr in clauses)


def module_dir(module: str, root: Path = ROOT) -> Path:
    return root / PROPERTIES / module


def load(module: str, root: Path = ROOT) -> dict:
    path = module_dir(module, root) / "manifest.toml"
    try:
        return tomllib.loads(path.read_text())
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise ToolError(f"error: cannot read {path.relative_to(root)}: {exc}") from exc


def hashes(module: str, root: Path = ROOT) -> dict:
    """The hashes the manifest must record for the files on disk now."""
    folder = module_dir(module, root)
    props = (folder / "props.c").read_text()
    stripped = stripped_headers(client_headers(props, root), root)
    return {"brief_sha256": sha256((folder / "brief.md").read_text()),
            "props_sha256": sha256(props),
            "headers": {path: sha256(text) for path, text in stripped.items()}}


def dump(manifest: dict) -> str:
    """TOML for the manifest: scalars, the headers table, then arrays of tables."""
    lines = []
    tables = []
    for key, value in manifest.items():
        if isinstance(value, dict):
            tables.append((key, value))
        elif isinstance(value, list):
            continue
        else:
            lines.append(f"{key} = {json.dumps(value)}")
    for key, value in tables:
        lines += ["", f"[{key}]"] + [f"{json.dumps(k)} = {json.dumps(v)}" for k, v in value.items()]
    for key, value in manifest.items():
        if isinstance(value, list):
            for entry in value:
                lines += ["", f"[[{key}]]"] + [f"{k} = {json.dumps(v)}" for k, v in entry.items()]
    return "\n".join(lines) + "\n"
