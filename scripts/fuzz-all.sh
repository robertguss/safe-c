#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

FUZZ_SECONDS="${FUZZ_SECONDS:-60}"
case "$FUZZ_SECONDS" in
    ''|*[!0-9]*)
        echo "error: FUZZ_SECONDS must be a non-negative integer (got '$FUZZ_SECONDS')" >&2
        exit 2
        ;;
esac

if [ -z "${LLVM:-}" ]; then
    if [ -d /opt/homebrew/opt/llvm ]; then
        LLVM=/opt/homebrew/opt/llvm
    elif [ -d /usr/local/opt/llvm ]; then
        LLVM=/usr/local/opt/llvm
    fi
fi

if [ -z "${CC:-}" ] || [ "${CC}" = "cc" ]; then
    if [ -n "${LLVM:-}" ]; then
        CC="${LLVM}/bin/clang"
    elif command -v clang >/dev/null 2>&1; then
        CC=clang
    elif command -v clang-18 >/dev/null 2>&1; then
        CC=clang-18
    else
        CC=clang
    fi
fi

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "error: missing required tool: $CC (set CC or LLVM to a clang toolchain with libFuzzer)" >&2
    exit 2
fi

probe_dir=$(mktemp -d "${TMPDIR:-/tmp}/safe-c-fuzz-probe.XXXXXX")
trap 'rm -rf "$probe_dir"' EXIT INT TERM

cat >"$probe_dir/probe.c" <<'EOF'
#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    (void)data;
    (void)size;
    return 0;
}
EOF

if ! "$CC" -std=c11 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
    "$probe_dir/probe.c" -o "$probe_dir/probe" >"$probe_dir/probe.log" 2>&1; then
    cat "$probe_dir/probe.log" >&2
    echo "error: $CC is missing required toolchain support: libFuzzer/ASan/UBSan (-fsanitize=fuzzer,address,undefined); on macOS install Homebrew llvm and set LLVM=/opt/homebrew/opt/llvm or CC=/opt/homebrew/opt/llvm/bin/clang" >&2
    exit 2
fi

if [ "$#" -gt 0 ]; then
    harnesses="$*"
else
    harnesses=""
    for candidate in fuzz/*_fuzz.c; do
        if [ -f "$candidate" ]; then
            harnesses="$harnesses $candidate"
        fi
    done
fi

if [ -z "$harnesses" ]; then
    echo "fuzz: no fuzz/*_fuzz.c harnesses"
    exit 0
fi

mkdir -p build/fuzz

for src in $harnesses; do
    if [ ! -f "$src" ]; then
        echo "error: fuzz harness not found: $src" >&2
        exit 2
    fi
    name=$(basename "$src" .c)
    out="build/fuzz/$name"
    if ! "$CC" -std=c11 -Werror -Wall -Wextra -Wvla -Wconversion -Wsign-conversion -g -O1 \
        -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
        "$src" -o "$out"; then
        echo "error: failed to build fuzz harness: $src" >&2
        exit 2
    fi
    if ! "$out" -artifact_prefix=build/fuzz/ -max_total_time="$FUZZ_SECONDS"; then
        exit 1
    fi
done
