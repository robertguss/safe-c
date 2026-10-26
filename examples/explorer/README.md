# Postgres explorer

A Postgres browser in the style of Postico 2, written in the Safe C subset
(`docs/subset-v1.md`) to test Safe C on an application that is not small. The plan is
`docs/plans/2026-10-25-2038-feat-postgres-explorer-plan.md`. Every rule fight,
missing module and workaround goes in [`BREAKAGE.md`](BREAKAGE.md).

U1 is the scaffold. The binary links SDL3, SDL3_ttf, libpq and the standard library,
then exits. U2 adds the window.

```
make -C examples/explorer          # builds build/explorer/explorer
make -C examples/explorer run      # builds and runs it
```

## Dependencies

The Makefile finds SDL3, SDL3_ttf and libpq through `pkg-config`.

- **macOS (Apple Silicon or Intel):** `brew install sdl3 sdl3_ttf libpq pkg-config`.
  Homebrew's libpq is keg-only; the Makefile already looks in
  `/opt/homebrew/opt/libpq/lib/pkgconfig` (and `/usr/local/opt/...` on Intel).
- **Linux:** Debian 12 and Ubuntu 24.04 ship no SDL3, so `sh scripts/install-sdl3.sh`
  builds the pinned SDL3 3.4.18 and SDL3_ttf 3.2.2 into `build/tools/sdl3`. It needs
  cmake, the FreeType, HarfBuzz, X11 and Wayland development packages, and
  `libpq-dev`. In an Amp orb, `.agents/setup` installs all of them and runs the
  script, and so does the Gates workflow's toolchain action. The CI image
  (`ci/Dockerfile`) builds the same pins into `/opt/sdl3`.

## Checks

`make lint` covers every `.c` and `.h` here. `make ci` builds the app as the
`explorer-build` step; `make gates` does not.
