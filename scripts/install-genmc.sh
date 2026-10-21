#!/bin/sh
# Build the pinned GenMC into PREFIX (default build/tools/genmc), where
# scripts/genmc.py looks for it. Needs LLVM 20 (llvm-20-dev, clang-20), g++ 14,
# libffi-dev, zlib1g-dev and libedit-dev; on Ubuntu 24.04:
#   sudo apt-get install llvm-20-dev clang-20 g++-14 libffi-dev zlib1g-dev libedit-dev
# Bump the version here, in scripts/genmc.py and in ci/Dockerfile together.
set -eu
VERSION=v0.19.0
COMMIT=9f6c4c0772d0c42b325681581acc6a0f3eb9b5f7
PREFIX=${1:-build/tools/genmc}
mkdir -p "$PREFIX"
PREFIX=$(cd "$PREFIX" && pwd)
SRC=$(mktemp -d)
trap 'rm -rf "$SRC"' EXIT
git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$VERSION" https://github.com/MPI-SWS/genmc.git "$SRC"
test "$(git -C "$SRC" rev-parse HEAD)" = "$COMMIT" || {
    echo "error: GenMC $VERSION is not commit $COMMIT" >&2
    exit 1
}
CC=${CC_GENMC:-gcc-14} CXX=${CXX_GENMC:-g++-14} cmake -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/lib/llvm-20/cmake -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -B "$SRC/build" -S "$SRC" >/dev/null
cmake --build "$SRC/build" -j "$(nproc)" >/dev/null
cmake --install "$SRC/build" >/dev/null
"$PREFIX/bin/genmc" --version
