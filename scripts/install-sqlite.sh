#!/bin/sh
set -eu
VERSION=3.54.1
ARCHIVE=3540100
SHA=0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9
PREFIX=${1:-build/tools/sqlite}
CC=${CC:-clang}
mkdir -p "$PREFIX"
PREFIX=$(cd "$PREFIX" && pwd)
PINS="prefix $PREFIX sqlite $VERSION $SHA $(uname -s) $(uname -m) $CC threadsafe no-load-extension"
if [ -f "$PREFIX/.pins" ] && [ "$(cat "$PREFIX/.pins")" = "$PINS" ] &&
   [ -f "$PREFIX/lib/libsqlite3.a" ] && [ -f "$PREFIX/src/sqlite3.c" ]; then
  echo "SQLite $VERSION already in $PREFIX"
  exit 0
fi
if [ -n "$(ls -A "$PREFIX")" ] && [ ! -f "$PREFIX/.pins" ]; then
  echo "install-sqlite: $PREFIX is not empty and holds no earlier install" >&2
  exit 1
fi
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
curl -fsSL -o "$TMP/sqlite.zip" "https://www.sqlite.org/2026/sqlite-amalgamation-$ARCHIVE.zip"
if command -v sha256sum >/dev/null 2>&1; then
  echo "$SHA  $TMP/sqlite.zip" | sha256sum -c - >/dev/null
else
  echo "$SHA  $TMP/sqlite.zip" | shasum -a 256 -c - >/dev/null
fi
unzip -q "$TMP/sqlite.zip" -d "$TMP"
mkdir -p "$TMP/stage/include" "$TMP/stage/lib" "$TMP/stage/src"
cp "$TMP/sqlite-amalgamation-$ARCHIVE/sqlite3.h" "$TMP/stage/include/"
cp "$TMP/sqlite-amalgamation-$ARCHIVE/sqlite3.c" "$TMP/stage/src/"
"$CC" -std=c11 -O2 -fPIC -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION \
  -I"$TMP/stage/include" -c "$TMP/stage/src/sqlite3.c" -o "$TMP/sqlite3.o"
ar rcs "$TMP/stage/lib/libsqlite3.a" "$TMP/sqlite3.o"
echo "$PINS" > "$TMP/stage/.pins"
rm -rf "$PREFIX"
mv "$TMP/stage" "$PREFIX"
echo "SQLite $VERSION installed in $PREFIX"
