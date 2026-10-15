#!/bin/sh
set -eu

PATH="${HOME}/.local/bin:${PATH}"
export PATH

cd "$(dirname "$0")/.."

collect() {
    file=$1
    sem_json=$(mktemp)
    tidy_out=$(mktemp)
    cc_out=$(mktemp)
    semgrep --config lint/semgrep/subset-v1.yml --metrics=off --json --quiet "$file" >"$sem_json" || true
    python3 - "$sem_json" << 'PY'
import json
import sys

data = json.load(open(sys.argv[1], encoding="utf-8"))
fatal = [err for err in (data.get("errors") or []) if err.get("level") == "error"]
if fatal:
    for err in fatal:
        print(err.get("message", "semgrep error"), file=sys.stderr)
    sys.exit(2)
for result in data.get("results") or []:
    print("lint-rule", result.get("check_id", ""))
PY
    clang-tidy --config-file=.clang-tidy "$file" -- -std=c11 >"$tidy_out" 2>&1 || true
    if grep -q 'misc-no-recursion' "$tidy_out"; then
        echo "lint-rule R10-no-recursion"
    fi
    if grep -q 'bugprone-narrowing-conversions' "$tidy_out"; then
        echo "lint-rule tidy-narrowing"
    fi
    clang -std=c11 -Werror -Wall -Wextra -Wvla -Wconversion -Wsign-conversion -fsyntax-only "$file" >"$cc_out" 2>&1 || true
    if grep -q -- '-Wvla' "$cc_out"; then
        echo "lint-rule R7-no-vla"
    fi
    if grep -E -q -- '-Wconversion|-Wsign-conversion|-Wfloat-conversion' "$cc_out"; then
        echo "lint-rule R11-no-implicit-conversion"
    fi
    rm -f "$sem_json" "$tidy_out" "$cc_out"
}

names_rule() {
    ident=$1
    printf '%s\n' "$2" | python3 -c 'import re, sys
ident = sys.argv[1]
text = sys.stdin.read()
ids = set(re.findall(r"(?m)^lint-rule .*?(R\d+)\b", text))
sys.exit(0 if ident in ids else 1)' "$ident"
}

if [ "$#" -eq 1 ]; then
    rules=$(collect "$1")
    printf '%s\n' "$rules"
    if printf '%s\n' "$rules" | grep -q '^lint-rule '; then
        exit 1
    fi
    exit 0
fi

if [ "$#" -ne 0 ]; then
    echo "usage: scripts/lint-selftest.sh [file]" >&2
    exit 2
fi

status=0
for file in tests/lint/bad/*.c; do
    base=$(basename "$file")
    ident=$(printf '%s\n' "$base" | sed -n 's/^\(R[0-9][0-9]*\)-.*/\1/p')
    if [ -z "$ident" ]; then
        echo "bad file has no rule id: $file" >&2
        status=1
        continue
    fi
    rules=$(collect "$file")
    if names_rule "$ident" "$rules"; then
        echo "bad $ident $file"
    else
        echo "missing $ident $file" >&2
        printf '%s\n' "$rules" >&2
        status=1
    fi
done

clean_rules=$(collect tests/lint/good/clean.c)
if printf '%s\n' "$clean_rules" | grep -q '^lint-rule '; then
    echo "clean file failed" >&2
    printf '%s\n' "$clean_rules" >&2
    status=1
else
    echo "pass tests/lint/good/clean.c"
fi

if [ "$status" -eq 0 ]; then
    echo "lint self-test passed"
fi
exit "$status"
