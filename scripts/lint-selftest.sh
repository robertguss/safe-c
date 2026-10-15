#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "$#" -eq 0 ]; then
    exec python3 scripts/lint.py --selftest
fi
exec python3 scripts/lint.py "$@"
