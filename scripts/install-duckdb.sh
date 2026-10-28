#!/bin/sh
set -eu
VERSION=2.0.0
PREFIX=${1:-build/tools/duckdb}
case "$(uname -s)/$(uname -m)" in
  Linux/x86_64) PLATFORM=linux-amd64; SHA=993ad89e7dfca66cd6da994c2cad090c2fc160ddf9f550498a098ab4bb13cfb1; LIB=libduckdb.so ;;
  Linux/aarch64|Linux/arm64) PLATFORM=linux-arm64; SHA=c60eecf91143ca6bbac55c4f4d40dc22520488b0ac257c86e0fe9a2b451cf2b7; LIB=libduckdb.so ;;
  Darwin/x86_64) PLATFORM=osx-amd64; SHA=98fef9fcad856555ca0403fe4d9d6680acfd26c67e8c6f004d61a248d6bc0eff; LIB=libduckdb.dylib ;;
  Darwin/arm64) PLATFORM=osx-arm64; SHA=c35d60080b31ba5d11a3ce505a6831be784f003b4c575d32a4c2badb3b41cdf0; LIB=libduckdb.dylib ;;
  *) echo 'install-duckdb: supported platforms are macOS and Linux glibc on x86-64/arm64' >&2; exit 1 ;;
esac
mkdir -p "$PREFIX"
PREFIX=$(cd "$PREFIX" && pwd)
PINS="prefix $PREFIX duckdb $VERSION $PLATFORM $SHA"
if [ -f "$PREFIX/.pins" ] && [ "$(cat "$PREFIX/.pins")" = "$PINS" ]; then
  echo "DuckDB $VERSION already in $PREFIX"
  exit 0
fi
if [ -n "$(ls -A "$PREFIX")" ] && [ ! -f "$PREFIX/.pins" ]; then
  echo "install-duckdb: $PREFIX is not empty and holds no earlier install" >&2
  exit 1
fi
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
curl -fsSL -o "$TMP/sdk.tar.gz" "https://github.com/duckdb/duckdb/releases/download/v$VERSION/duckdb-shared-libs-$PLATFORM.tar.gz"
if command -v sha256sum >/dev/null 2>&1; then
  echo "$SHA  $TMP/sdk.tar.gz" | sha256sum -c - >/dev/null
else
  echo "$SHA  $TMP/sdk.tar.gz" | shasum -a 256 -c - >/dev/null
fi
tar -xzf "$TMP/sdk.tar.gz" -C "$TMP"
mkdir -p "$TMP/stage/include" "$TMP/stage/lib"
cp "$TMP/duckdb.h" "$TMP/stage/include/"
cp "$TMP/$LIB" "$TMP/stage/lib/"
echo "$PINS" > "$TMP/stage/.pins"
rm -rf "$PREFIX"
mv "$TMP/stage" "$PREFIX"
echo "DuckDB $VERSION installed in $PREFIX"
