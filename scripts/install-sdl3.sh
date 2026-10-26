#!/bin/sh
# Build the pinned SDL3 and SDL3_ttf into PREFIX (default build/tools/sdl3),
# where examples/explorer/Makefile looks for them through pkg-config. Linux
# needs this because Ubuntu 24.04 and Debian 12 ship no SDL3 package; on macOS
# use `brew install sdl3 sdl3_ttf` instead. SDL3_ttf builds against the
# system FreeType and HarfBuzz (libfreetype-dev, libharfbuzz-dev), because its
# release tarball does not carry the vendored copies. SDL needs the X11 and
# Wayland development headers (libx11-dev ... libgbm-dev, listed in
# .agents/setup and ci/Dockerfile) to build a video backend.
#
# The tarballs are pinned by SHA-256, so a changed upstream file fails the
# check instead of changing the build. Bump a version and its checksum here and
# in ci/Dockerfile together. A finished install writes $PREFIX/.pins; a later
# run with the same pins and prefix does nothing.
set -eu
SDL_VERSION=3.4.18
SDL_SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
TTF_VERSION=3.2.2
TTF_SHA256=63547d58d0185c833213885b635a2c0548201cc8f301e6587c0be1a67e1e045d
PREFIX=${1:-build/tools/sdl3}

mkdir -p "$PREFIX"
PREFIX=$(cd "$PREFIX" && pwd)
# The .pc files and the rpath record the absolute prefix, so a moved checkout rebuilds.
PINS="prefix $PREFIX sdl $SDL_VERSION $SDL_SHA256 ttf $TTF_VERSION $TTF_SHA256"
if [ -f "$PREFIX/.pins" ] && [ "$(cat "$PREFIX/.pins")" = "$PINS" ]; then
  echo "SDL3 $SDL_VERSION and SDL3_ttf $TTF_VERSION already in $PREFIX"
  exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)

quiet() { # run a build step; print its log only if it fails
  "$@" >"$TMP/step.log" 2>&1 || { tail -n 40 "$TMP/step.log"; exit 1; }
}

fetch() { # url sha256 file
  curl -fsSL -o "$TMP/$3" "$1"
  echo "$2  $TMP/$3" | sha256sum -c - >/dev/null
  tar -xzf "$TMP/$3" -C "$TMP"
}

fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz" \
  "$SDL_SHA256" sdl.tar.gz
fetch "https://github.com/libsdl-org/SDL_ttf/releases/download/release-$TTF_VERSION/SDL3_ttf-$TTF_VERSION.tar.gz" \
  "$TTF_SHA256" ttf.tar.gz

# The prefix is wiped before the build; refuse one this script did not create.
if [ -n "$(ls -A "$PREFIX")" ] && [ ! -f "$PREFIX/.pins" ]; then
  echo "install-sdl3: $PREFIX is not empty and holds no earlier install; refusing to wipe it" >&2
  exit 1
fi
rm -rf "$PREFIX"
mkdir -p "$PREFIX"
quiet cmake -S "$TMP/SDL3-$SDL_VERSION" -B "$TMP/build-sdl" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
  -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_STATIC=OFF
quiet cmake --build "$TMP/build-sdl" -j "$JOBS"
quiet cmake --install "$TMP/build-sdl"

quiet cmake -S "$TMP/SDL3_ttf-$TTF_VERSION" -B "$TMP/build-ttf" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="$PREFIX" \
  -DSDLTTF_VENDORED=OFF -DSDLTTF_PLUTOSVG=OFF -DSDLTTF_SAMPLES=OFF
quiet cmake --build "$TMP/build-ttf" -j "$JOBS"
quiet cmake --install "$TMP/build-ttf"

PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" pkg-config --modversion sdl3 sdl3-ttf
echo "$PINS" > "$PREFIX/.pins"
