# Postgres explorer

A Postgres browser in the style of Postico 2, written in the Safe C subset
(`docs/subset-v1.md`) to test Safe C on an application that is not small. The plan is
`docs/plans/2026-10-25-2038-feat-postgres-explorer-plan.md`. Every rule fight,
missing module and workaround goes in [`BREAKAGE.md`](BREAKAGE.md).

So far (milestone 5) it connects to one server, browses its catalog, and runs the SQL
typed into a highlighting editor, showing the rows, the command tag or the error in
Postico's query view, in light or dark following the system theme. `theme.c` holds Postico's
palette and metrics.

```
make -C examples/explorer             # builds build/explorer/explorer
make -C examples/explorer run         # builds and runs it with the PG* environment
make -C examples/explorer screenshot  # renders SCENE (default hello) to build/explorer/SCENE.png
make -C examples/explorer screenshots # renders every scene into build/explorer/scenes/
make -C examples/explorer pq-test     # the worker against a temporary server
make -C examples/explorer pq-test-18  # the same against libpq 18 (sh scripts/install-libpq.sh)
```

## Running it

```
build/explorer/explorer                                  # connects with PG* and ~/.pgpass
build/explorer/explorer --conninfo 'host=localhost dbname=app user=me'
```

With no `--conninfo` (or an empty one), libpq takes the server, user and password from
`PGHOST`, `PGUSER`, `PGPASSWORD`, `~/.pgpass` and the rest of its environment, so no
secret has to sit on the command line.

⌘ below is the Command key on macOS and Ctrl on Linux. On macOS, Ctrl stays its own
modifier: ^A and ^E move to the start and end of the line.

Each key goes to one place: the window's own keys (⌘↩, ⌘., ⌥⌘., ⌘0, ⌥⌘0 and the tab
keys) work wherever the focus is; the rest go to the area that has focus,
the editor, the grid or the sidebar, and otherwise do nothing. With the sidebar
focused, ⌘↩ opens the selected object in a new tab instead of running the SQL. Typing goes to the editor only
while it has focus. A left click focuses the area it lands on.

| Key | Does |
| --- | --- |
| ⌘↩ or Execute | runs the selection, or else the statement holding the caret (shaded grey); a run sends at most 127 KiB |
| ⌥⌘↩ | runs the whole editor |
| ⌘. or Cancel | cancels the selected tab's run: a waiting tab leaves the queue; while connecting, the connection is dropped |
| ⌥⌘. or Disconnect | disconnects, keeping the rows on screen (Disconnect replaces Cancel after 5 s of cancelling) |
| ↩, Backspace, Delete, ←→↑↓ | edit the SQL; Tab inserts two spaces |
| ⌥← / ⌥→, ⌘← / ⌘→ or Home/End, ⌘↑ / ⌘↓ | move by word, to the line's ends, to the document's ends; with ⇧ they select |
| ^A, ^E (macOS) | move to the start or end of the line |
| ⌘A | selects all; the next typing or paste replaces it |
| ⌘Z / ⇧⌘Z | undo / redo (100 steps; a typing run within 500 ms is one) |
| ⌘C, ⌘X, ⌘V | copy, cut and paste with the system clipboard; a paste goes in whole or not at all (at most 1 MiB) |
| ⌘/ , ⌘] / ⌘[ | toggle `-- ` on the selected lines, indent / outdent them by two spaces |
| click, drag, double / triple click (editor) | place the caret, select, select a word / a line |
| Esc (editor) | cancels an input-method composition |
| click a row | selects it and gives the grid the keyboard; a click on the editor takes it back |
| ↑↓, Page Up/Down, ⌘↑/⌘↓ (grid) | move the selected row, scrolling the least that shows it |
| Esc (grid) | clears the selection |
| ⌥⌘0 | shows or hides the row-detail pane |
| ⌘0 | shows or hides the sidebar |
| ↑↓ (sidebar) | move the selection |
| → / ← (sidebar) | expand a schema / collapse it, or move from an object to its schema |
| ↩ or a click (sidebar) | opens a table or view in the current tab; on a schema, expands or collapses it |
| ⌥⌘← / ⌥⌘→, or ‹ › in the toolbar | go back and forward through the tab's views |
| ⌥⌘T, or the Content / Structure switch | shows a table's rows or its structure |
| ‹ › in a table's bottom bar | the previous or next page of rows |
| ⌘↩ or ⌘-click (sidebar) | opens it in a new tab |
| wheel or trackpad | scrolls the grid or the editor under the pointer |
| ⇧⌘↩ | Load all: runs again with no row limit, only the memory quota |
| ⌘T or the + | opens a tab |
| ⌘1 to ⌘9, or a click | selects a tab |
| ⌘W or the tab's × | closes a tab, cancelling its query if it is running |

The editor holds up to 1 MiB of UTF-8 text without control characters (but tab and
newline), moves and deletes by character, colours keywords, strings and comments,
and shows input-method composition underlined at the caret; a byte from the server that is not UTF-8 shows as U+FFFD. The result grid is
virtualised: each frame lays out only the rows and columns on screen, and seeks to the
first visible row through the result's index.
Numbers are right-aligned, NULL is grey, and a newline or tab in a cell shows as ↵ or
→. The row-detail pane lists the selected row's columns with their types and wraps
each value.

Each tab has its own SQL and result; all share the one connection. A tab whose run
finds the connection busy says "Waiting for connection…" and runs when the queries
before it end, in the order they were started. A streaming tab shows a dot in its
title. A tab keeps its result's memory until it closes or runs again, so with large
results in several tabs a run can end at the memory limit. A failed or lost connection,
or a Disconnect, says so in the toolbar and offers Reconnect there and in the centre
view: it opens a new connection with the same settings, every tab keeps its rows, and
waiting tabs run once it connects. The old connection drains in the background; while
two are still draining, Reconnect waits ("Waiting for the old connection"). TCP
keepalives turn a dropped network into a lost connection within about 25 s. Quitting
waits up to 2 s in all for the workers to close; one still blocked is left to the
process exit.

With libpq 15 or 16 a cancel blocks the worker until the server answers it; libpq 17
and later cancel without blocking, with a 10 s deadline. A cancel unanswered by then
reads "Cancel failed".

Once the query connection is up, a second, metadata connection opens with the same
settings (`application_name=explorer-meta`, a 10 s `statement_timeout`) and reads the
catalog one statement at a time, so the sidebar never waits behind a query. The
sidebar is one box with a Tables | Functions switch and a reload button (it reads the
catalog again, keeping the expanded schemas) over the schema tree. A schema lists its
tables, partitioned tables, views, materialised views and foreign tables, each with its
own glyph and colour, or in Functions mode its functions and procedures; a schema still
loading shows "Loading…", and one whose read failed shows the error and a Retry row.
Opening a relation shows it in the tab as a table view, and the tab keeps the views
it showed (up to 16) for back and forward; going back to the query view brings back
its SQL, with no result and no run. Back and forward abandon a page that is still
running, but not a query you ran: while the query view's run is going or waiting,
‹ › are greyed and the keys say a query is running. Content shows the rows a page of 1,000 at a time,
in primary key order (`ctid` with no key; a view or foreign table has no order, and a
note says so), and the pager reads "Page k of ~N" from the planner's estimate until a
`count(*)` on the metadata connection says "Page k of N"; a count that takes over 2 s
gives up and the pager says "Page k". A page the connection refused says why, with
Reconnect when the connection is down, and a table dropped since it was opened says
it is gone. Structure lists every column with its chips
(primary key, not null, unique, the table a foreign key points at, identity,
generated), then the indexes and the check and foreign-key constraints. Opening a function opens a new tab
holding its `pg_get_functiondef` text, not run. If the metadata connection fails or is lost
while queries still work, the box shows the error with its own Retry. While the query
connection is down it keeps the list, greyed, under "Disconnected", and Reconnect
rebuilds both connections.

`make screenshots` renders the U8 scenes `query-rows`, `query-wide`, `query-tag`, `query-error`,
`query-error-rows`, `conn-auth-failed` and `conn-disconnected`, and the U11 scenes
`grid-light`, `grid-dark`, `grid-null-wrap`, `grid-error-after-rows`, `grid-capped` and
`grid-detail`, the U12 scenes `tabs-streaming` (it then quits mid-stream, which
must take under 2 s) and `grid-million`, which loads a million rows, scrolls them in
201 frames and logs the frame times, and the U13 scenes `query-cancelling` (Cancel
mid-stream), `query-disconnect-offer` (a cancel left unanswered for 5 s offers
Disconnect) and `conn-reconnect` (the connection lost under 300 rows, which stay, with
Reconnect offered; after the screenshot it reconnects and runs `SELECT 1`), and the U18
scene `sidebar-meta-lost` (the metadata connection's backend ended: the sidebar's error
and Retry), and the U19 scenes `sidebar-light` and `sidebar-dark` (four schemas with
every kind of relation, one table opened from the sidebar) and `sidebar-error` (the
Functions list, a function's definition opened in a new tab, and a schema whose read
failed, with its Retry row), and the U20 scenes `table-content-light` and
`table-content-dark` (a 2,500-row table opened from the sidebar, its count landed),
`table-structure`, `table-view-note` (a view's first page and its note) and
`table-back` (page 2, back to the query view while the page runs, then forward to page
2 again; the scene fails if a check does), and the U22 scenes `editor-light` and
`editor-dark` (two statements: keywords, strings and comments coloured, the band on
the statement at the caret, and a selection) and `editor-compose` (a composition
underlined at the caret), against a temporary
server that `pgtest.sh` starts, so it needs the Postgres server binaries
(`pg_config --bindir`).

## Shape

- `adapters/ui.c` and `adapters/ui_input.c` are the only files that touch SDL3,
  SDL3_ttf or Clay (rule R0). `ui.c` owns the C `main` and calls `app_main` with the
  arguments as `sc_str` values; `ui_input.c` holds events, text input, the clipboard
  and the cursors.
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
- `--scene <name> --screenshot <png>` renders one scene and exits; add
  `--outline-tags` to outline every tagged rectangle, where clicks land. With
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
(every `tests/explorer_*_test.c`: frame, theme, shell, input, focus, tabs, query text, result, grid, detail, proto,
conn, wire, budget, catalog, tree, nav, page, and the view tests). `make ci` builds the app as the
`explorer-build` step; `make gates` does not.
