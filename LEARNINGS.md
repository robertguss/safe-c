# Learnings

Facts that cost time to discover. Delete an entry when it stops being true.

## Tooling

- `scripts/sanitize.py test` takes no source arguments (only `fuzz` does). To run one test, compile it with clang and the `SAN_FLAGS` from `scripts/sanitize.py`.
- The orb has no gdb or lldb. Print before the failing assert.
- A truncated `proto` cell must carry a true length greater than its stored bytes, or `proto_get_cell` rejects the chunk.
- Four links on one pool use all 8 control arenas (`BUDGET_CONTROL_ARENAS`). A fifth `link_open` still returns true, but its OPEN is refused (`CONN_REFUSE_POOL`) and the link stays in `UI_CONN_NONE`. In pq-test, close a helper link before filling both retiring slots next to a query and metadata link.
- A test that `#include`s several explorer `.c` files is one translation unit, so every `static` in them needs a unique name.
- Tool output can show a source line as `[REDACTED:…]` (for example `PGUSER`/`PGPASSWORD` in `examples/explorer/pgtest.sh`). Rewriting the line from that output corrupts it; restore it with `git show HEAD:<file>`.
- Inter (the UI font) has U+25B6/U+25BC (▶▼), U+203A (›), U+2304 (⌄), U+0192 (ƒ) and U+21BB (↻), but not the small triangles U+25B8/U+25BE; JetBrains Mono has those. Check a glyph with `uv run --with fonttools` and `TTFont(f).getBestCmap()` before drawing it.
- SDL_ttf caches only 8 shaped strings. A frame that measures dozens of strings (the sidebar) thrashes that cache, so `ui_text_size` keeps its own 1024-slot width cache, cleared with the fonts.
- `tree_click` ignores a click when the row count or first row differ from the last `tree_visible`. A tree test that clicks after something changes the rows must call `tree_visible` again first.
- A test that builds a view with `frame_begin` must put any string the view copies (`v.message`, labels) in the frame arena *after* the arena's reset, or `frame_text` fails the epoch check and the build fails with nothing printed.
- `pgtest.sh` listens on a fixed port (55432) unless `PGTEST_PORT` is set, so a `make screenshot` run while pq-test is running needs `PGTEST_PORT=<other>`.
- `make -C examples/explorer <path>/pq_asan` fails with `libpq-fe.h` not found: the libpq flags are set only when a `pq-test*` goal is named. Build through `pq-test`.
- The orb has no Frama-C. Start Docker with `amp orb service start dockerd --command 'sudo dockerd'`, then run the provers in `ghcr.io/robertguss/safe-c-ci:latest` (`sudo docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/src" -w /src <image> make prove PROVE_SRC=...`). The tag `make ci-docker` asks for (`scripts/ci.py --image-tag`) may not be published, and building `ci/Dockerfile` locally compiles Frama-C through opam.
- WP with `-wp-rte` raises no goal for pointer arithmetic on NULL (`NULL + 0`), which UBSan reports. A guard against it survives `make mutate` as `if-cond->0` unless an ACSL `assert p != \null;` sits where the offset is taken.
- `make mutate MUTATE_SRC=stdlib/buf.c` runs 105 mutants at about a minute of WP each: about 65 minutes with `MUTATE_JOBS=2`.
- In `tests/explorer_pq_integration.c`, a scenario that calls `meta_start(CATALOG_DB)` must run after `catalog_fixtures`, which creates that database. `schema_named` aborts when the schema is not listed, which is the case just after a reload or reconnect.
- `grid-million` frame-build times vary by about 30% between orb sessions. Compare a branch against `origin/main` built in a worktree and run alternately, never against an old number.
- SDL's offscreen video driver has no clipboard backend, but `SDL_SetClipboardText` still stores the text in process and `SDL_GetClipboardText` returns it, so a headless scene can round-trip the clipboard for real.
- SDL copies no text from a pushed `SDL_EVENT_TEXT_EDITING` or `SDL_EVENT_TEXT_INPUT` event: the pushed `text` pointer must stay valid until the event is read (`ui_input.c` keeps scripted text until `ui_close`).
- `SDL_EVENT_TEXT_EDITING`'s `start` and `length` count characters, not bytes, and are -1 when not set.
- SDL 3.4's Cocoa backend ignores `SDL_HINT_IME_IMPLEMENTED_UI` and the text-input type (`SDL_PROP_TEXTINPUT_TYPE_NUMBER`, so a hidden password too): it always sends `SDL_EVENT_TEXT_EDITING` with the plain preedit. Only X11, IBus, fcitx and Windows read the hint.

## libpq

- libpq 18's chunked rows mode holds rows until a chunk fills, drops the partial chunk on an error, and puts the command tag on the last chunk. The adapter uses `PQsetSingleRowMode` on every version.
- On libpq 17+, a cancel still pending when its query ends can land on the next query. The worker holds the next query until the cancel reports SENT or FAILED.
- libpq's explicit `options` key replaces both `PGOPTIONS` and a `pg_service.conf` entry's options, and `PQconninfoParse` never reads the service file. The adapter passes no `options`; the metadata link sets its `statement_timeout` with a SQL `SET` instead.
- PgBouncer (1.20+) refuses `-c statement_timeout=...` in the startup `options` ("unsupported startup parameter in options"). Set it in SQL.
- `PGSERVICEFILE=<path>` plus `service=<name>` in the conninfo is enough to test service-file handling against `pgtest.sh`.
- `pgtest.sh` listens on a Unix socket and on 127.0.0.1. Keepalives and the libpq 17+ cancel's TCP phases need the TCP port.
- Building libpq 18 from source needs bison, flex and libssl-dev.

## Postgres catalog (PG 15)

- `pg_get_indexdef`, `pg_get_constraintdef` and `pg_get_viewdef` wait behind an `ACCESS EXCLUSIVE` lock on the table; `pg_attribute`, `pg_class` and `format_type` reads do not.
- `inet_server_addr()` is NULL over a Unix socket; the session read shows `localhost` then (`COALESCE(host(...), 'localhost')`).
- `pg_get_indexdef(oid, 0, true)` drops the schema when it is on `search_path`.
- With another session holding `ACCESS EXCLUSIVE` on `pg_catalog.pg_namespace`, even `SELECT current_setting(...)` blocks, but a bare `SET` does not. New connections still open.
- PG 18 adds virtual generated columns: `attgenerated = 'v'`.
- `CREATE ROLE ... CONNECTION LIMIT 1` makes a second connection fail with "too many connections for role", a no-hook way to make one of two links fail to connect.
- `attidentity` and `attgenerated` are empty text, not NULL, when unset.
- `UPDATE pg_class SET reltuples = 1e30` (as superuser) makes `reltuples::int8` fail with "bigint out of range": a no-hook way to make one schema's relations read fail in a scene.
- `pg_get_functiondef` ends with a newline, so splitting it on `\n` gives a last empty line.
- `pg_get_functiondef` raises an error for an aggregate ("... is an aggregate function"). The functions read leaves out `prokind = 'a'`.
- `CREATE TABLE U&"t\0001" (...)` and `U&"bad\007Fname"` make tables whose names hold a control character or DEL: a no-hook fixture for names the input box refuses.
- Every PG scene in `make screenshots` shares one server, so a scene's fixture SQL must be idempotent (the sidebar scenes wrap theirs in `DO $$ ... IF NOT EXISTS ... $$`).
