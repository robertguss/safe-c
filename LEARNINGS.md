# Learnings

Facts that cost time to discover. Delete an entry when it stops being true.

## Tooling

- `scripts/sanitize.py test` takes no source arguments (only `fuzz` does). To run one test, compile it with clang and the `SAN_FLAGS` from `scripts/sanitize.py`.
- The orb has no gdb or lldb. Print before the failing assert.
- A truncated `proto` cell must carry a true length greater than its stored bytes, or `proto_get_cell` rejects the chunk.
- Four links on one pool use all 8 control arenas (`BUDGET_CONTROL_ARENAS`). A fifth `link_open` still returns true, but its OPEN is refused (`CONN_REFUSE_POOL`) and the link stays in `UI_CONN_NONE`. In pq-test, close a helper link before filling both retiring slots next to a query and metadata link.
- A test that `#include`s several explorer `.c` files is one translation unit, so every `static` in them needs a unique name.
- Tool output can show a source line as `[REDACTED:…]` (for example `PGUSER`/`PGPASSWORD` in `examples/explorer/pgtest.sh`). Rewriting the line from that output corrupts it; restore it with `git show HEAD:<file>`.
- `grid-million` frame-build times vary by about 30% between orb sessions. Compare a branch against `origin/main` built in a worktree and run alternately, never against an old number.

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
- `pg_get_indexdef(oid, 0, true)` drops the schema when it is on `search_path`.
- With another session holding `ACCESS EXCLUSIVE` on `pg_catalog.pg_namespace`, even `SELECT current_setting(...)` blocks, but a bare `SET` does not. New connections still open.
- PG 18 adds virtual generated columns: `attgenerated = 'v'`.
- `CREATE ROLE ... CONNECTION LIMIT 1` makes a second connection fail with "too many connections for role", a no-hook way to make one of two links fail to connect.
- `attidentity` and `attgenerated` are empty text, not NULL, when unset.
