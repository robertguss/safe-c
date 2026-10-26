# Postgres explorer

A Postgres browser in the style of Postico 2, written in the Safe C subset
(`docs/subset-v1.md`) to test Safe C on an application that is not small. The plan is
`docs/plans/2026-10-25-2038-feat-postgres-explorer-plan.md`. Every rule fight,
missing module and workaround goes in [`BREAKAGE.md`](BREAKAGE.md).

So far (milestone 2) it connects to one server, runs the SQL typed into a plain
input box, and shows the rows, the command tag or the error in Postico's query
view, in light or dark following the system theme. `theme.c` holds Postico's
palette and metrics.

```
make -C examples/explorer             # builds build/explorer/explorer
make -C examples/explorer run         # builds and runs it with the PG* environment
make -C examples/explorer screenshot  # renders SCENE (default hello) to build/explorer/SCENE.png
make -C examples/explorer screenshots # renders every scene into build/explorer/scenes/
make -C examples/explorer pq-test     # the worker against a temporary server
```

## Running it

```
build/explorer/explorer                                  # connects with PG* and ~/.pgpass
build/explorer/explorer --conninfo 'host=localhost dbname=app user=me'
```

With no `--conninfo` (or an empty one), libpq takes the server, user and password from
`PGHOST`, `PGUSER`, `PGPASSWORD`, `~/.pgpass` and the rest of its environment, so no
secret has to sit on the command line.

| Key | Does |
| --- | --- |
| ⌘↩ or Ctrl+↩ | runs the whole input box |
| Esc | cancels the running query |
| ↩, Tab, Backspace, ←→↑↓ | edit the SQL |
| ⌘A or Ctrl+A | selects all; the next typing or paste replaces it |
| ⌘V or Ctrl+V | pastes, whole or not at all (at most 64 KiB) |
| click a row | selects it and gives the grid the keyboard; a click on the input box takes it back |
| ↑↓, Page Up/Down, ⌘↑/⌘↓ (grid) | move the selected row, scrolling the least that shows it |
| Esc | cancels a running query, else (grid focused) clears the selection |
| ⌥⌘0 | shows or hides the row-detail pane |
| wheel or trackpad | scrolls the grid |
| ⇧⌘↩ or Ctrl+Shift+↩ | Load all: runs again with no row limit, only the memory quota |
| ⌘T or the + | opens a tab |
| ⌘1 to ⌘9, or a click | selects a tab |
| ⌘W or the tab's × | closes a tab, cancelling its query if it is running |

The input box takes UTF-8 text without control characters, and moves and deletes by
character; a byte from the server that is not UTF-8 shows as U+FFFD. The result grid is
virtualised: each frame lays out only the rows and columns on screen, and seeks to the
first visible row through the result's index.
Numbers are right-aligned, NULL is grey, and a newline or tab in a cell shows as ↵ or
→. The row-detail pane lists the selected row's columns with their types and wraps
each value.

Each tab has its own SQL and result; all share the one connection. A tab whose run
finds the connection busy says "Waiting for connection…" and runs when the queries
before it end, in the order they were started. A streaming tab shows a dot in its
title. A tab keeps its result's memory until it closes or runs again, so with large
results in several tabs a run can end at the memory limit. A failed or lost connection says so in the toolbar; until reconnect
arrives (U13), quit and relaunch. Quitting waits up to 2 s for the worker to close
the connection.

`make screenshots` renders the U8 scenes `query-rows`, `query-wide`, `query-tag`, `query-error`,
`query-error-rows`, `conn-auth-failed` and `conn-disconnected`, and the U11 scenes
`grid-light`, `grid-dark`, `grid-null-wrap`, `grid-error-after-rows`, `grid-capped` and
`grid-detail`, and the U12 scenes `tabs-streaming` (it then quits mid-stream, which
must take under 2 s) and `grid-million`, which loads a million rows, scrolls them in
201 frames and logs the frame times, against a temporary
server that `pgtest.sh` starts, so it needs the Postgres server binaries
(`pg_config --bindir`).

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

`make lint` covers every `.c` and `.h` here, and `make test` runs the core tests
(every `tests/explorer_*_test.c`: frame, theme, shell, input, query text, result, grid, detail, proto,
conn, wire). `make ci` builds the app as the
`explorer-build` step; `make gates` does not.
