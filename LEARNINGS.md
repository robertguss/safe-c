# Learnings

Facts that cost time to discover. Delete an entry when it stops being true.

## Tooling

- Explorer pane limits must reserve the result's error and reconnect strips, not only the query toolbar. Rows followed by an error with detail and hint otherwise push the footer out at a small window size.
- SDL's offscreen driver has no resize cursors. Verify the real cursor with X11 and `XFixesGetCursorImage`; SDL auto-captures the mouse while a button is held, including divider releases outside the window.
- `frame_cmd` deliberately rejects Control on macOS. A Control-Command chord must test the raw modifier bits before ordinary Command routing; on Linux history uses Ctrl+Super so Ctrl+Left keeps word movement.
- History offsets change on eviction. Keep a dropped-entry ordinal when a tab needs to refer back to its last retained query, and compare SQL bytes rather than editor versions: undo changes the version even when it restores the saved text.
- `SDL_GetPrefPath("Safe C", "Explorer")` gives the directory used by all explorer settings/history. Headless scenes must not save there; native persistence checks use a disposable `XDG_DATA_HOME`.
- Reject an empty preference directory before joining filenames: SDL can return no path when directory creation fails, and a bare `history` filename would otherwise target the working directory.
- R13 is structural, not type-aware: a config pair field named `value` triggers the result guard. Use `text`, and keep reads of actual result payloads inside a positive `.ok` branch when cleanup makes an early-out too complex for the rule.
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
- `make -C examples/explorer <path>/pq_asan` fails with `libpq-fe.h` not found: the libpq flags are set only when a `pq-test*` goal is named. Build through `pq-test`.
- The orb has no Frama-C. Start Docker with `amp orb service start dockerd --command 'sudo dockerd'`, then run the provers in `ghcr.io/robertguss/safe-c-ci:latest` (`sudo docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/src" -w /src <image> make prove PROVE_SRC=...`). The tag `make ci-docker` asks for (`scripts/ci.py --image-tag`) may not be published, and building `ci/Dockerfile` locally compiles Frama-C through opam.
- Root Make exports an unset `FUZZ_SECONDS` as an empty environment value. CI defaults must handle both absent and empty values while passing nonempty values through to the positive-integer validator.
- Frama-C 33's default WP cache can replay a timeout at the same budget without rerunning that prover. A quiet retry against the failed PR 96 cache still timed out on `lex_dollar`; cold and `FRAMAC_WP_CACHE=rebuild` runs proved both complete files at the unchanged 10-second budget. Preserve the failed cache before diagnosing it. These passes do not establish why the original run timed out or replace a failed full CI run.
- WP with `-wp-rte` raises no goal for pointer arithmetic on NULL (`NULL + 0`), which UBSan reports. A guard against it survives `make mutate` as `if-cond->0` unless an ACSL `assert p != \null;` sits where the offset is taken.
- `make mutate` proves each mutant with `prove.py --function` (Frama-C `-wp-fct`), so a mutant costs only its function's goals: str.c, slice.c and utf8.c took 255 s instead of 570 s on 16 jobs, and sqltok.c's 395 mutants 1150 s on 12.
- WP's Qed simplifier can grow without bound on a long `&&` chain of calls checked against one nested logic definition (sqltok.c's old `word_kind` reached 24 GB). Split the C so each function matches one small definition. Never run `frama-c -wp -wp-par 16` on a whole explorer file without `-wp-fct`; it can use most of a 31 GB orb.
- An exact lexer contract (`\result == tk_step(...)`) kills mutants that per-property contracts let through; fuzz the logic against an independent reference that emits tokens (end and kind), not only split offsets.
- `quote_body` joined the quote scanner's branches into one exact-token postcondition that timed out in an isolated reproduction. Splitting that postcondition into token fields still timed out. Separate scalar `tk_qend` scanning, `tk_qclose` recognition, and token assembly instead; retain the exact token contract and prove each helper.
- For the current three-field `sqltok_token`, equality of `kind`, `end`, and the whole `next` state implies exact token equality, checked by a WP client. Those projections make the dollar helpers' postconditions smaller without changing their runtime code or public contract. Omitting a field would weaken the guarantee.
- In `tests/explorer_pq_integration.c`, a scenario that calls `meta_start(CATALOG_DB)` must run after `catalog_fixtures`, which creates that database. `schema_named` aborts when the schema is not listed, which is the case just after a reload or reconnect.
- `grid-million` frame-build times vary by about 30% between orb sessions. Compare a branch against `origin/main` built in a worktree and run alternately, never against an old number.
- SDL's offscreen video driver has no clipboard backend, but `SDL_SetClipboardText` still stores the text in process and `SDL_GetClipboardText` returns it, so a headless scene can round-trip the clipboard for real.
- SDL copies no text from a pushed `SDL_EVENT_TEXT_EDITING` or `SDL_EVENT_TEXT_INPUT` event: the pushed `text` pointer must stay valid until the event is read (`ui_input.c` keeps scripted text until `ui_close`).
- `SDL_EVENT_TEXT_EDITING`'s `start` and `length` count characters, not bytes, and are -1 when not set.
- SDL 3.4's Cocoa backend ignores `SDL_HINT_IME_IMPLEMENTED_UI` and the text-input type (`SDL_PROP_TEXTINPUT_TYPE_NUMBER`, so a hidden password too): it always sends `SDL_EVENT_TEXT_EDITING` with the plain preedit. Only X11, IBus, fcitx and Windows read the hint.
- An empty `sc_str` still carries its arena's epoch, and `sc_buf_str` rejects a source whose epoch is stale even when it has no bytes. Re-home empty slices too before resetting the arena they came from (the editor's `$$` checkpoints silently stopped on this).
- A run's SQL crosses the link through a fixed 128 KiB private arena; `LINK_SQL_MAX` (127 KiB) is the longest that fits there and in the 256 KiB RUN message arena next to libpq's copy.
- The editor is `editor.c` plus `editor_syntax.c`; a single-translation-unit test or fuzz harness that includes one must include both.
- The editor scenes run queued SQL against Postgres before rendering: caret-only movement preserves the queued range, text edits re-pick it, a selection differs from its enclosing statement, and run-all keeps the last result set. Scratch mutations that always or never re-pick both fail the scene.

## Explorer engine boundaries

- Database-control atomics live in `adapters/db_control.c`, shared by the real engine adapter and pure-core links tests. Keep that source in `LINK_ADAPTERS`; including the whole dispatcher would make the pure-core test depend on engine SDKs.
- `worker.c:start_query` reuses the RUN arena for the terminal reply immediately after `sc_pq_send_query` copies SQL. A new engine that prepares a batch incrementally must own the SQL before that reset.
- The worker drains cancellation messages between nonblocking libpq operations. A blocking embedded-engine call cannot receive cancellation through that channel while it runs; its cancellation signal must remain reachable independently, with connection/request lifetime protection.
- `conncfg.c` strips `#` comments before trimming lines. Its current grammar cannot preserve arbitrary native filenames; do not reuse it for file targets without changing that contract.
- DuckDB 2.0.0 must receive `temp_directory` before `enable_external_access=false`, and `lock_configuration=true` last. Setting the spill directory after disabling external access fails.
- DuckDB's `duckdb_value_to_string` produces SQL literals, including temporal casts. `duckdb_get_varchar` produces display strings without routing exact decimals or 128-bit integers through double.
- DuckDB ENUM labels can contain NUL. In pinned 2.0.0, v2 `vector_get_value` and `value_to_string` expose the exact display byte count and bytes. The length excludes the terminator, but the output buffer needs one extra byte. The two APIs share native Vector handles in this pin, but not connection handles. Recheck that boundary on an SDK upgrade, and install both C headers.
- DuckDB query-local temporary tables can shadow `schema.table` even with a read-only database file. Capture `current_database()` from the metadata session and quote all three identifiers for Content. A filename-derived database name is not a substitute for the native name.
- The pinned DuckDB stream uses `ChunkFormat::UnpackUnit`, whose freshly copied vectors are flat. Read only the current chunk, check `duckdb_result_error` after a NULL fetch, and destroy it before advancing the native batch.
- DuckDB's `max_temp_directory_size` limits its buffer-manager accounting, not a filesystem quota. That manager decreases its counter before truncating a file. Do not promise a strict disk-byte ceiling from that setting, or a total-process RSS ceiling from `memory_limit`.
- Use `posix_spawn` for the DuckDB integration test's independent writer process. Forking after native-engine initialization exhausted clang 18 TSan's 64-entry lock detector before the test could run; spawning passes without disabling race or deadlock detection.
- DuckDB 2.0.0's prebuilt library reports a TSan race between allocation and a pipeline mutex when `threads=2`, reproducible with a standalone native streaming loop without Explorer or cancellation. Explorer caps engine execution at one thread; the adapter's query, metadata and interrupt threads still run concurrently and remain TSan-instrumented. A green adapter run does not prove the uninstrumented SDK internals race-free.
- SQLite's authorizer sees `pragma_*` table-valued functions as reads of `sqlite_master` with an empty database argument, and function events put the function name in the second argument. Permit the safe introspection shape without permitting ATTACH, load_extension or writable PRAGMAs. Read live WAL files normally, not with `immutable=1`; the integration fixture keeps a writer open and verifies a committed WAL-only row.
- DuckDB 2.0.0 reports `duckdb_columns().column_index` from one and constraint column indexes from zero. Convert constraint indexes before joining the shared column model. Its list-transform lambda syntax is `lambda x: x + 1`; the former arrow syntax is disabled by default.
- SQLite's `INTEGER PRIMARY KEY DESC` is not a rowid alias and may be NULL. A sole INTEGER primary key is implicitly non-null only when `pragma_index_list` has no primary-key index; WITHOUT ROWID keys already report non-null through `pragma_table_xinfo`. Foreign-key column matching must ignore ASCII case.
- SQLite trigger completeness follows `; END;`, not a count of BEGIN and END words. Ordinary columns can have either name inside a trigger. Match native `sqlite3_complete` boundaries without splitting inner statements.
- SQLite accepts `EXPLAIN` and `EXPLAIN QUERY PLAN` before a compound `CREATE TRIGGER`. The splitter must recognize the prefix before tracking the trigger body, or caret execution can select an inner statement.
- SQLite named parameters allow Tcl suffixes such as `$x::y(a';b)`. The suffix can contain quotes and semicolons. Treat the whole parameter as one token when selecting a statement.
- Catalog providers reserve more SQL scratch than the old 4 KiB app arena. Use `META_SQL_BYTES` in both the app and integration fixtures; an oversized test arena can hide catalog failures in the real UI.
- UI tags are global across view modules. `panes.h` owns 0x411 and 0x412; a Definition button at 0x411 made `ui_frame_valid` reject local Structure frames while leaving the old Content frame visible. The view test must build a local Structure frame with its sidebar shown.

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
- `CREATE TABLE U&"t\0001" (...)` and `U&"bad\007Fname"` make tables whose names hold a control character or DEL: a no-hook fixture for names the editor refuses.
- Every PG scene in `make screenshots` shares one server, so a scene's fixture SQL must be idempotent (the sidebar scenes wrap theirs in `DO $$ ... IF NOT EXISTS ... $$`).
