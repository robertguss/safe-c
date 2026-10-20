#!/bin/sh
# Installs Safe C into build/pkg-prefix, then builds and lints the sample adopter
# from a scratch copy outside the repo using only the installed files.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
prefix=$root/build/pkg-prefix
rm -rf "$prefix"
make -C "$root" install PREFIX="$prefix"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -R "$root/examples/adopter/." "$work/"
cp "$root/tests/package/subset_break.c" "$work/subset_break.c"
cd "$work"

PATH=$prefix/bin:$PATH
PKG_CONFIG_PATH=$prefix/lib/pkgconfig
export PATH PKG_CONFIG_PATH

test "$(pkg-config --modversion safec)" = "$(cat "$root/VERSION")"
make all

safec-lint main.c
if safec-lint subset_break.c >lint.out 2>&1; then
    echo "package-test: rule pack did not flag subset_break.c" >&2
    cat lint.out >&2
    exit 1
fi
grep -q 'lint-rule R1-no-subscript' lint.out
echo "package-test: ok"
