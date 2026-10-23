#!/bin/sh
# Download the pinned TLA+ proof manager (with its bundled Z3, Zenon and
# Isabelle backends) into PREFIX (default build/tools/tlapm), where
# scripts/tlaps.py looks for it. 1.6.0-pre is a rolling pre-release, so a
# changed upstream tarball fails the checksum instead of changing the prover;
# bump the checksum here and in ci/Dockerfile, and the commit in
# scripts/tlaps.py, together after checking the proofs.
set -eu
URL=https://github.com/tlaplus/tlapm/releases/download/1.6.0-pre/tlapm-1.6.0-pre-x86_64-linux-gnu.tar.gz
SHA256=0d1b393a1042aa08961bb024c784a80a25009a5874258c0ddbbfd95b92e80fe5
PREFIX=${1:-build/tools/tlapm}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
curl -fsSL -o "$TMP/tlapm.tar.gz" "$URL"
echo "$SHA256  $TMP/tlapm.tar.gz" | sha256sum -c - >/dev/null
tar -xzf "$TMP/tlapm.tar.gz" -C "$TMP"
rm -rf "$PREFIX"
mkdir -p "$(dirname "$PREFIX")"
mv "$TMP/tlapm" "$PREFIX"
"$PREFIX/bin/tlapm" --version
