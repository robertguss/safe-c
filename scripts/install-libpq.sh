#!/bin/sh
# Build only libpq, from the pinned PostgreSQL 18 release, into PREFIX (default
# build/tools/libpq18), where examples/explorer/Makefile finds it for the
# explorer and `pq-test-18` (plan KTD20). The orb's and Ubuntu's libpq (15, 16)
# lack the non-blocking cancel that Robert's Mac (Homebrew libpq 18) uses, so
# this compiles and tests that path on Linux. It
# leaves the system libpq alone: the PGDG packages would replace it.
#
# Needs a C compiler, make, bison, flex and the OpenSSL headers (libssl-dev),
# all listed in .agents/setup and ci/Dockerfile. The tarball is pinned by
# SHA-256, so a changed upstream file fails the check instead of changing the
# build. Bump the version and its checksum here and in ci/Dockerfile together.
# A finished install writes $PREFIX/.pins; a later run with the same pins and
# prefix does nothing. The install goes into a staging copy first ($PREFIX.new,
# with .pins written last) and replaces the prefix in one move, so a run cut
# short leaves the old install, or none, never a part of one.
set -eu
PG_VERSION=18.6
PG_SHA256=983ee554ec53dbeb9b70797bef9fcf4e67e117e7e48ca1463cc80b3ff8e8ff3f
PREFIX=${1:-build/tools/libpq18}

mkdir -p "$PREFIX"
PREFIX=$(cd "$PREFIX" && pwd)
# libpq.pc and the explorer's rpath record the absolute prefix, so a moved checkout rebuilds.
PINS="prefix $PREFIX postgresql $PG_VERSION $PG_SHA256"
if [ -f "$PREFIX/.pins" ] && [ "$(cat "$PREFIX/.pins")" = "$PINS" ]; then
  echo "libpq $PG_VERSION already in $PREFIX"
  exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)

quiet() { # run a build step; print its log only if it fails
  "$@" >"$TMP/step.log" 2>&1 || { tail -n 40 "$TMP/step.log"; exit 1; }
}

curl -fsSL -o "$TMP/pg.tar.gz" "https://ftp.postgresql.org/pub/source/v$PG_VERSION/postgresql-$PG_VERSION.tar.gz"
echo "$PG_SHA256  $TMP/pg.tar.gz" | sha256sum -c - >/dev/null
tar -xzf "$TMP/pg.tar.gz" -C "$TMP"

# The prefix is replaced at the end; refuse one this script did not create.
if [ -n "$(ls -A "$PREFIX")" ] && [ ! -f "$PREFIX/.pins" ]; then
  echo "install-libpq: $PREFIX is not empty and holds no earlier install; refusing to wipe it" >&2
  exit 1
fi
SRC="$TMP/postgresql-$PG_VERSION"
# Only what libpq needs: no server-side libraries. OpenSSL, as Homebrew's has.
(cd "$SRC" && quiet ./configure --prefix="$PREFIX" --with-openssl --without-readline \
  --without-icu --without-zlib --without-zstd --without-lz4)
quiet make -C "$SRC/src/interfaces/libpq" -j "$JOBS"
# DESTDIR keeps the recorded prefix (libpq.pc, the rpath) while the files land
# in the staging tree.
quiet make -C "$SRC/src/interfaces/libpq" install DESTDIR="$TMP/stage"
# postgres_ext.h and pg_config_ext.h, which libpq-fe.h includes.
quiet make -C "$SRC/src/include" install DESTDIR="$TMP/stage"

STAGE="$PREFIX.new"
rm -rf "$STAGE"
mv "$TMP/stage$PREFIX" "$STAGE"
PKG_CONFIG_PATH="$STAGE/lib/pkgconfig" pkg-config --modversion libpq
echo "$PINS" > "$STAGE/.pins"
rm -rf "$PREFIX"
mv "$STAGE" "$PREFIX"
