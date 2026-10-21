#!/bin/sh
# Download the pinned TLA+ proof manager (with its bundled Z3, Zenon and
# Isabelle backends) into PREFIX (default build/tools/tlapm), where
# scripts/tlaps.py looks for it. 1.6.0-pre is a rolling pre-release, so a
# changed upstream tarball fails the checksum instead of changing the prover;
# bump the checksum here and in ci/Dockerfile together after checking the proofs.
set -eu
URL=https://github.com/tlaplus/tlapm/releases/download/1.6.0-pre/tlapm-1.6.0-pre-x86_64-linux-gnu.tar.gz
SHA256=b3c2554d3dd2198772ea8c814639a024fe9c390eac0ec25f4f444ad446a7dd28
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
