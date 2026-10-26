# Postgres explorer

A Postgres browser in the style of Postico 2, written in the Safe C subset
(`docs/subset-v1.md`) to test Safe C on an application that is not small. The plan is
`docs/plans/2026-10-25-2038-feat-postgres-explorer-plan.md`. Every rule fight,
missing module and workaround goes in [`BREAKAGE.md`](BREAKAGE.md).

So far (U2) it opens a window and draws text in Inter and JetBrains Mono.

```
make -C examples/explorer             # builds build/explorer/explorer
make -C examples/explorer run         # builds and runs it
make -C examples/explorer screenshot  # renders the hello scene to build/explorer/hello.png
```

## Shape

- `adapters/ui.c` is the only file that touches SDL3, SDL3_ttf or Clay (rule R0). It
  owns the C `main` and calls `app_main` with the arguments as `sc_str` values.
- The app describes each frame as a list of `ui_node` records in an arena
  (`adapters/sc_ui_frame.h`): each node names its parent by index, in depth-first
  order, so the adapter rebuilds the tree with no recursion. `frame.c` builds that
  list and is core code; `tests/explorer_frame_test.c` covers it. The adapter calls
  the same `ui_frame_valid` before it hands a frame to Clay.
- A custom node is a box whose contents the app draws itself as `ui_draw_item`
  records (rectangles, lines, text), offset to the box and clipped to it. The result
  grid will use this (plan KTD3); the hello scene has a small table drawn this way.
- A frame that fails anywhere (bad shape, missing font, Clay layout error) is not
  shown, and `ui_draw` returns false.
- Coordinates are integer points. The adapter scales to pixels for Retina displays
  and opens fonts at the pixel size, so text stays sharp.
- `--scene <name> --screenshot <png>` renders one scene and exits. With
  `SDL_VIDEO_DRIVER=offscreen` it needs no display; agents use it to check the look.

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

Fonts: Inter 4.1 and JetBrains Mono 2.304 under the SIL Open Font Licence, in `fonts/`
with their licence texts. The Makefile copies them next to the binary.

## Checks

`make lint` covers every `.c` and `.h` here, and `make test` runs
`tests/explorer_frame_test.c`. `make ci` builds the app as the
`explorer-build` step; `make gates` does not.
