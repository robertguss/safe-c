# Learnings

Facts that cost time to discover. Delete an entry when it stops being true.

## Tooling

- `scripts/sanitize.py test` takes no source arguments (only `fuzz` does). To run one test, compile it with clang and the `SAN_FLAGS` from `scripts/sanitize.py`.
- The orb has no gdb or lldb. Print before the failing assert.
- A test that `#include`s several explorer `.c` files is one translation unit, so every `static` in them needs a unique name.
- Tool output can show a source line as `[REDACTED:…]` (for example `PGUSER`/`PGPASSWORD` in `examples/explorer/pgtest.sh`). Rewriting the line from that output corrupts it; restore it with `git show HEAD:<file>`.
- `grid-million` frame-build times vary by about 30% between orb sessions. Compare a branch against `origin/main` built in a worktree and run alternately, never against an old number.

## libpq

- libpq 18's chunked rows mode holds rows until a chunk fills, drops the partial chunk on an error, and puts the command tag on the last chunk. The adapter uses `PQsetSingleRowMode` on every version.
- On libpq 17+, a cancel still pending when its query ends can land on the next query. The worker holds the next query until the cancel reports SENT or FAILED.
- libpq's explicit `options` key replaces `PGOPTIONS`; the adapter merges the two itself.
- `pgtest.sh` listens on a Unix socket and on 127.0.0.1. Keepalives and the libpq 17+ cancel's TCP phases need the TCP port.
- Building libpq 18 from source needs bison, flex and libssl-dev.

## Postgres catalog (PG 15)

- `pg_get_indexdef`, `pg_get_constraintdef` and `pg_get_viewdef` wait behind an `ACCESS EXCLUSIVE` lock on the table; `pg_attribute`, `pg_class` and `format_type` reads do not.
- `pg_get_indexdef(oid, 0, true)` drops the schema when it is on `search_path`.
- `attidentity` and `attgenerated` are empty text, not NULL, when unset.
