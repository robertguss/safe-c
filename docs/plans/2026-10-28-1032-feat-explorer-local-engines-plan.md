---
title: Explorer DuckDB and SQLite Support - Plan
type: feat
date: 2026-10-28
artifact_contract: ce-unified-plan/v1
product_contract_source: ce-plan-bootstrap
execution: code
---

# Explorer DuckDB and SQLite Support - Plan

## Goal Capsule

- **Objective:** Robert can browse and query his local DuckDB and SQLite databases in Explorer, using the editor and results experience he already uses for PostgreSQL.
- **Means:** native C adapters behind a small three-engine boundary, sharing Explorer's result protocol and UI (KTD1).
- **Authority:** Robert's current request and approvals govern product behavior; current source governs existing behavior. This proposal supersedes only the PostgreSQL-only scope of `docs/plans/2026-10-25-2038-feat-postgres-explorer-plan.md`. That document's automatic PR/merge language does not authorize delivery of this work.
- **Execution profile:** planning is finalized; product decisions A1–A4, including the opening flow and exact wording, are approved. Implementation is not authorized. Pushing, opening a PR, and merging need separate authorization; none is supplied by this plan. Robert runs native macOS builds and acceptance tests himself.
- **Stop conditions:** do not implement until Robert explicitly authorizes implementation. During implementation, stop rather than weaken local-file protection, silently materialize unbounded results, change the approved design/wording or Safe C subset, or add a proof waiver. Return those decisions to Robert.

---

## Product Contract

### Summary

Add local DuckDB and SQLite sessions alongside PostgreSQL, with catalog and structure browsing, SQL editing/execution, and the existing results UI. Robert approved the restricted access policy (A1), one active database per window (A2), catalog browsing scope (A3), and opening flow with exact wording (A4). No product/design decisions remain before requesting implementation authorization.

### Problem Frame

Robert wants Explorer to become his daily database GUI and specifically lacks a DuckDB GUI he wants to use. The current application is comfortable for him, but its connection and database behavior are PostgreSQL-only. Supporting two file-based engines should not turn this request into a redesign or a general database-driver platform.

### Requirements

**Database work**

- R1. Open existing local DuckDB and SQLite database files, with an explicit engine identity and useful open errors.
- R2. Browse namespaces, tables, views, columns, keys, indexes, and available definitions through the existing sidebar, Content, and Structure views; identify unavailable metadata rather than inventing PostgreSQL equivalents.
- R3. Edit and execute SQL in the selected engine and reuse results, NULL display, truncation, copying, row details, cancellation, and retained tab results.
- R4. Keep PostgreSQL connection settings, authentication, metadata, SQL behavior, paging, cancellation, and history working as before.

**Safety and acceptance**

- R5. Make local-file access mode explicit, avoid accidental database creation, and document what read-only opening does and does not protect.
- R6. Keep database work off the UI thread and preserve the existing bounded result-storage and arena-ownership contracts.
- R7. Support reproducible macOS/Linux dependencies without adding database dependencies to `libsafec.a` or to pure-core unit tests.
- R8. Preserve the existing appearance, wording, and pane-resizing work; obtain Robert's approval for new controls and labels before implementing them.
- R9. Return this plan before implementation and preserve the separate implementation and delivery gates in the Goal Capsule.

### Key Decisions

- **A1. Restricted local access for the first version** (session-settled: user-approved — chosen over writable or external-access local sessions: Robert approved the restricted first version on 2026-10-28). Open existing SQLite and DuckDB databases **read-only**, with external access and extensions disabled. External CSV/Parquet-backed DuckDB views are outside this version. PostgreSQL behavior is unchanged. **Governs R1, R3, R5.** This settles the access policy only; it does not approve the remaining scope, UI design/wording, implementation, push, PR creation or merge. A1 retains its original ID after moving from Assumptions so existing references remain valid.
- **A2. One active database per window** (session-settled: user-approved — chosen over different databases in different tabs: preserve the existing session model and keep query targets clear). All tabs share that database, and New Session switches databases through the existing unsaved-editor flow. Robert approved this first-version scope on 2026-10-28. **Governs R1, R3.** A2 retains its original ID after moving from Assumptions. This does not approve the remaining browsing/opening-flow choices, implementation or delivery.
- **A3. Browse stored tables and views in the opened file** (session-settled: user-approved — chosen over broader session-object browsing: cover stored-data inspection without adding session-local catalog management). SQLite exposes `main`; DuckDB exposes schemas within the opened database. Include tables/views, columns, keys, indexes and available definitions. Defer attached/temporary objects and function/macro browsing. Robert approved this first-version scope on 2026-10-28. **Governs R2.** A3 retains its original ID after moving from Assumptions. This decision does not authorize implementation or delivery.
- **A4. Existing picker and native file opening** (session-settled: user-approved — chosen over a separate file-management screen or second open confirmation: preserve the existing workflow). Robert approved the opening flow and exact wording below on 2026-10-28, including immediate opening after file selection, unchanged picker on dialog cancellation, and no new recent-files screen. **Governs R1, R4, R5, R8.** A4 retains its original ID after moving from Assumptions. This approval is for design/wording only; implementation and delivery remain unauthorized.

### Scope Boundaries

No web frontend, ORM, plugin registry, server process, or replacement results renderer is needed. PostgreSQL remains a first-class engine, not a compatibility mode implemented through DuckDB.

Considered and not built: a custom database-file sandbox/VFS or per-query subprocess. Native adapters cannot guarantee containment of hostile engine exploits; process isolation would be a separate security feature. Reconsider if opening adversarial files becomes a requirement.

Considered and not built: automatic file repair, WAL recovery by opening writable, copying live databases without their journals, or automatic version conversion. These can alter or misrepresent the user's data.

#### Deferred to Follow-Up Work

Under approved A1: local writes, database creation, external CSV/Parquet/network readers, external-file export and extensions are outside the first version. Under approved A2: simultaneous database sessions in one window are excluded. Under approved A3: attached/temporary objects and function/macro browsing are deferred. Under approved A4: no new recent-files screen. Import/export UI, attachment management and encryption/SQLCipher are not part of the requested first-version work. No implementation unit quietly adds these capabilities.

### Authorization Gates

- **No outstanding product/design decisions.** A1–A4 are settled. The technical checkpoints below are implementation verification, not unresolved product choices.
- **Blocking implementation — permission to begin:** Robert must explicitly authorize implementation. Design/wording approval is not that authorization.
- **Delivery remains separate:** no push, PR creation or merge is authorized.

### Approved Opening Flow and Exact Wording (A4)

- Add an engine choice to the existing connection picker: **PostgreSQL**, **DuckDB**, **SQLite**. Keep its existing visual style and PostgreSQL's **Connect to PostgreSQL** heading, saved-connection list, **Connect**, and authentication flow unchanged.
- For a local engine, show **Open DuckDB database** or **Open SQLite database**, with **Open Database…** and **Cancel**. Show **Read-only. External access and extensions are disabled.** No writable toggle is offered.
- **Open Database…** launches the native file dialog. Choosing an existing file begins opening it immediately; there is no second confirmation step. Cancelling the dialog leaves the picker unchanged. During opening show **Opening…**; on failure keep the picker available and show **Could not open database** with the path and a specific reason, without password/network wording.
- New Session retains the approved A2 flow and its existing unsaved-SQL confirmation. Do not add a recent-files screen or change PostgreSQL connection persistence.

---

## Planning Contract

### Implementation Readiness

The plan is ready for implementation authorization, not authorized execution. A1–A4 are settled. Preserve the approved flow and wording; any deviation needs Robert's approval. A CLI equivalent for automation uses the same target validation and access policy, and file extensions only filter the native chooser rather than establish format validity.

After authorization, start from main containing the merged pane changes and run the unchanged PostgreSQL baseline before U1. U2's streaming, cancellation, memory and file-safety checkpoint remains a technical stop/go gate before UI integration, not a promise already verified during planning.

### Existing Architecture and Reuse

Grounding: inspected source at `9935588f437bc0721520752711a5ffccec3410fa`, then inspected the GitHub diff for pane resizing at [ae6fa8b](https://github.com/robertguss/safe-c/commit/ae6fa8b6071430a99e843aa62616a7460a213d65), reported merged on main by Puck. That diff keeps pane state on the window-level app, adds pane sources to the Makefile, and changes pointer/focus routing without changing engine ownership. Robert reported both dividers working on his Mac. This planning checkout has not incorporated that commit; use merged main and re-read affected integration code before implementation.

| Area | Reuse | PostgreSQL-specific work |
| --- | --- | --- |
| Ownership/lifecycle | `conn.c`, `conn_worker.c`, `wire.c`, `budget.c`: connection/request IDs, arena credit, terminal reserve, retained results, retirement | Do not replace these machines with engine-owned queues; adapt new execution events to them |
| Execution | `worker.c` result pump, chunk sealing, tags, error-after-rows | `sc_pq_wait`, socket readiness, flush, connect polling, COPY drain, async cancel are libpq behavior |
| Results | `result.c`, `grid.c`, `copy.c`, `view_grid.c`, `tabs.c` | COLUMNS carries 32-bit type OIDs; `result_numeric` and `detail_type_name` interpret PG OIDs; 1,664 columns is an application bound inherited from PG |
| Catalog | `catalog.c` lazy reads, loading/error states, generation checks; `meta.c` separate metadata link | `catalog_sql.c` uses `pg_catalog`, OID filters, PG booleans/arrays and `SET statement_timeout`; session fields assume server/user/host |
| Paging/structure | `page.c` page state, Content/Structure rows, counts and labels | No-key ordering uses `ctid` or `tableoid, ctid`; constraints/indexes and generated-column fields reflect PG |
| Editor | Gap buffer, undo, selection, queued range, UTF-8 rendering | `sqltok.c/.h` and `editor_syntax.c` use PG dollar strings, nested comments, escape strings and `standard_conforming_strings`; `sqlkw.c` has one keyword set |
| Sessions/history | `links.c` query/meta/retiring slots; `store.c` atomic saves; `history.c` bounded format | OPEN holds conninfo/password; `conncfg.c` accepts PG keys; `picker.c` defaults to Environment; history identifies entries by display name, not a durable database key |

The traced execution path is `main.c:start` → `link_run_limit` → RUN arena → worker state-machine actions → adapter → COLUMNS/CHUNK/terminal replies → `link_poll` → tab result. The worker resets the RUN arena into the terminal reply after libpq copies SQL. An embedded adapter must retain its own SQL while preparing later statements; borrowing the overwritten RUN body is invalid.

Cancel currently travels over the same request channel that the worker drains between nonblocking libpq operations. Replacing `sc_pq_next` with a blocking engine call would prevent that worker from reading Cancel. Retirement deliberately leaves live worker storage allocated until join; an embedded database owner must obey the same lifetime.

### Key Technical Decisions

- KTD1. **Use native SQLite and DuckDB C APIs through a small tagged adapter boundary.** Add `adapters/sc_db.h` and `adapters/db.c`, dispatching only the three supported engine kinds to `pq.c`, new `sqlite.c`, and new `duckdb.c`. Expose connection, execution events, row/column serialization, cancellation and close, not a portable SQL language. Keep libpq's socket/cancel mechanics inside its path. First route PostgreSQL through the boundary without changing observable behavior. A DuckDB SQLite extension was rejected because it changes SQLite semantics and adds extension distribution to a native-file feature. A vtable/plugin registry and three copies of the worker pump were rejected as unnecessary complexity. These alternatives can be judged from the existing ownership boundary; no competing implementation bake-off is needed. Governs R1, R3, R4, R6.

- KTD2. **Keep query and metadata workers, with explicit embedded-session ownership.** SQLite gets two independent read-only connections. DuckDB gets one database instance for the opened canonical file and separate query/meta connections. An adapter-owned session handle holds the instance until all current and retiring workers release it; reconnect must not close it underneath a retiring query. Reject incompatible access/configuration instead of reopening the same file with different options. Connections and results stay worker-owned. Local metadata sees committed database state, not temporary objects or the query connection's private transaction snapshot. This matches the current independent metadata model within A3.

- KTD3. **Embedded cancellation needs a separate path from arena messages.** Add a request-scoped cancellation signal owned by the stable link slot. SQLite's progress and bounded busy handlers observe it during prepare/step and lock waits. DuckDB uses an adapter-owned interrupt coordinator that can call `duckdb_interrupt` while execution or fetch blocks. It must retain the connection, match both connection generation and request ID, and continue observing a pending cancellation through engine startup until the worker acknowledges termination; a one-shot interrupt before DuckDB resets its query state can be lost. It never touches result arenas. The normal protocol still decides terminal status and returns credits. Finalize/destroy the active statement/result before accepting the next request; disarm interruption before close, and join the coordinator before freeing its handle. Keep PG's existing late-cancel hold unchanged. Reuse the current Disconnect/retiring behavior when cancellation stalls; do not promise hard cancellation of filesystem I/O. [SQLite progress](https://www.sqlite.org/c3ref/progress_handler.html), [interrupt lifetime](https://www.sqlite.org/c3ref/interrupt.html), and [DuckDB interrupt implementation](https://github.com/duckdb/duckdb/blob/v2.0.0/src/main/client_context.cpp) ground this boundary.

- KTD4. **Stream into existing result credits; separate engine memory from result memory.** SQLite advances one prepared statement with `sqlite3_step`. DuckDB consumes data chunks from a streaming prepared/pending result and serializes rows only while credit exists. Keep at most the current engine chunk plus bounded formatting scratch, not a second full result copy. The documented streaming entry points remain deprecated in v2.0.0; contain that dependency in `duckdb.c` and pin the library rather than claiming a stable replacement exists. On a NULL fetched chunk, inspect the engine error before declaring success. Preserve row/cell/quota caps and last-result-set behavior. Retain the 1,664-column bound as an explicit Explorer limit and report a query error before accepting an oversized schema, rather than marking the link broken. DuckDB operators and large scalar values can allocate outside the arena pool: use explicit engine memory/thread limits and a bounded app-owned spill directory, then measure RSS and disk use. Do not call the result quota a total-process memory limit. [Pinned C header](https://github.com/duckdb/duckdb/blob/v2.0.0/src/include/duckdb.h).

- KTD5. **Normalize display metadata, not values into PG types.** Replace the UI's universal OID assumption with a type label and small display class, retaining optional native type identity for PostgreSQL. Keep PG type names/alignment behavior unchanged. SQLite columns may mix storage classes: retain declared type where available, otherwise show unknown/mixed and use conservative alignment; do not infer an entire column from its first row. Distinguish NULL, empty text, empty blob, and invalid text. Render blobs as bounded hex and DuckDB decimals, 128-bit integers, temporal and nested values without lossy double conversion. Preserve original length/truncation semantics for the serialized display bytes and make binary display/copy semantics explicit in the approved wording. Error codes become engine-tagged; never fabricate SQLSTATE or a PG failed-transaction state for local engines. Convert known native error offsets to the existing character-position convention; show no caret position when unavailable.

- KTD6. **Split catalog SQL and decoding by engine while retaining the catalog model.** Keep PG queries and decoding intact behind their provider. DuckDB reads `duckdb_schemas()`, `duckdb_tables()`, `duckdb_views()`, `duckdb_columns()`, `duckdb_constraints()` and `duckdb_indexes()`, scoped to the opened database and excluding internal objects. SQLite reads `sqlite_schema` and table-valued `pragma_table_xinfo`, `pragma_index_list`, `pragma_index_xinfo`, and `pragma_foreign_key_list`. Preserve composite-key order and generated/hidden distinctions. SQLite CHECK expressions and generated expressions are not fully exposed by those PRAGMAs: show the stored CREATE definition through the existing editor rather than write a DDL parser or silently present an empty constraints list as complete. Internal autoindexes have no CREATE SQL; label them from native metadata. Definitions must use a bounded multi-field/chunk path, not the 4 KiB cell cap with silent truncation. Embedded object references use database/schema/name/kind plus a catalog generation; transient numeric IDs never survive reload as identity. Re-resolve saved navigation names on reload and mark missing objects gone. Preserve PG native-OID identity semantics. Qualify/quote identifiers and bind or correctly escape metadata string arguments; names from a database are not trusted SQL. [DuckDB catalog](https://duckdb.org/docs/current/sql/meta/duckdb_table_functions), [SQLite PRAGMAs](https://www.sqlite.org/pragma.html).

- KTD7. **Keep LIMIT/OFFSET paging, with engine-specific ordering.** PG keeps its existing primary-key/`ctid` rules. Local tables with a proven unique, non-null primary key use its columns in key order. SQLite's nullable non-integer primary keys are not sufficient; WITHOUT ROWID keys are. Local tables without a suitable key and views use the existing unordered-page notice; do not introduce hidden-rowid shadowing bugs or promise snapshot-stable pages. Counts keep the current two-second budget and unknown-total fallback, implemented through local execution deadlines rather than PG SET statements. No invented row estimates: use unknown unless native metadata provides a suitable estimate. External writes can move rows across OFFSET pages; refresh does not freeze a snapshot.

- KTD8. **Make lexical behavior explicit per engine; let engines parse execution batches.** Extend the current tokenizer/checkpoint state with dialect, preserving the PG default and `standard_conforming_strings`. SQLite needs backtick/bracket identifiers and nonnested block comments, without PG dollar/E-string semantics. Verify DuckDB's quoting/comment rules against the pinned engine rather than equating PG compatibility with lexical identity. Engine-specific keyword sets affect highlighting only. Current-statement selection must not split a SQLite trigger body into executable inner statements: recognize compound ranges, or refuse automatic selection of an unsupported compound form and require an explicit selection/run-all. Keep selection precedence and queued-edit re-picking. SQLite executes the native prepare tail sequentially; DuckDB uses native extracted statements, preparing each only after the preceding statement completes. Stop the batch on error, cancellation or cap; retain partial rows and the last set under the existing contract. Do not emulate PG implicit batch-transaction semantics on embedded engines. Their native transaction behavior must be documented and tested.

- KTD9. **Enforce A1 at engine and adapter boundaries, not with keyword filtering.** Resolve a nonempty native path to an existing regular file off the UI thread; reject NULs, directories, special devices, URI inputs and memory-database sentinels. Resolve symlinks to the target for identity and display; a path check is not a defense against hostile replacement races. SQLite uses `SQLITE_OPEN_READONLY`, no CREATE/URI behavior, trusted schema off, defensive mode, extension loading disabled, and an authorizer that denies writes, ATTACH/DETACH, unsafe PRAGMAs and policy changes while allowing read introspection/transactions. `sqlite3_stmt_readonly` alone is insufficient: it regards ATTACH as read-only. DuckDB uses `access_mode=READ_ONLY`, external access off, extension autoload/autoinstall off, unsigned extensions off, and locked configuration with no user allowlist. Reject unsupported policy configuration rather than opening with weaker defaults. Never retry writable. SQLite WAL sidecars may be required or created by its locking machinery; preserve them and never set `immutable=1` on an arbitrary live file. A DuckDB file held by another process for writing produces an explicit lock error, not a copy/recovery workaround. These controls protect intended file access, not against every malicious-file exploit. [SQLite open](https://www.sqlite.org/c3ref/open.html), [security](https://www.sqlite.org/security.html), [WAL](https://www.sqlite.org/wal.html), [read-only statements](https://www.sqlite.org/c3ref/stmt_readonly.html), [DuckDB security](https://duckdb.org/docs/current/operations_manual/securing_duckdb/overview), [concurrency](https://duckdb.org/docs/current/connect/concurrency).

- KTD10. **Keep persistence compatible and avoid a new settings system.** OPEN carries a tagged target: PG conninfo/password or local path/access policy. Local failures bypass password/network wording. Do not put local paths through `conncfg.c` unchanged: its `#` comment stripping and whitespace trimming lose valid filename bytes. Under A4 leave `connections.conf` PG-only and backward-compatible. Keep history format v1; new local entries use an unambiguous engine plus canonical-path display label, while old PG labels stay untouched. History recall inserts SQL into the current editor without auto-opening a file or executing it. A moved/missing file fails on reopening; no path-search heuristic. Save settings/history only in the existing SDL preference directory, never beside the database.

- KTD11. **Pin embedded dependencies separately from the standalone library.** Initial candidates are DuckDB 2.0.0 (released October 21, 2026) and SQLite 3.54.1 (October 16). The DuckDB installation page returned older version labels than its release page, so the pinned release/header, not a moving “stable” URL, owns compatibility. Add checked-download installers under `scripts/`, with exact archive digests and matching headers. Use DuckDB shared-library archives for macOS arm64/x86-64 and Linux glibc x86-64/arm64, with platform rpaths; do not assume Homebrew's CLI formula supplies the same C SDK. Build the small SQLite amalgamation with thread safety and no loadable extensions into a private prefix on both platforms; do not depend on macOS's system SQLite version. Keep all-three-engine dependencies scoped to Explorer; pure-core and PG integration targets remain runnable without embedded libraries. Test version/architecture mismatch and loader behavior. Linux proves Linux only; Robert owns the native Mac check. No installers or binaries were run during planning. [DuckDB release/assets](https://github.com/duckdb/duckdb/releases/tag/v2.0.0), [SQLite source/download](https://sqlite.org/download.html).

### High-Level Technical Design

The existing ownership path stays common; engine-specific behavior sits beneath it:

```diagram
┌───────────────────────────────────────────────┐
│ Existing editor, tabs, catalog and results UI │
└──────────────────────┬────────────────────────┘
                       │ tagged session / RUN / CANCEL
┌──────────────────────▼────────────────────────┐
│ Links, state machines, arena credits, worker  │
└──────────────────────┬────────────────────────┘
                       │ execution events and display fields
┌──────────────────────▼────────────────────────┐
│ Three-engine adapter: PostgreSQL/SQLite/DuckDB │
└───────────────────────────────────────────────┘
```

Embedded request lifetime, including the out-of-band cancel path:

```diagram
┌─────────┐    ┌──────────────────────┐    ┌───────────────────┐
│ Queued  │───▶│ Prepare / step/fetch │───▶│ Finalize / destroy│
└─────────┘    └──────────┬───────────┘    └─────────┬─────────┘
                         ▲                        │
               request-scoped cancel              ▼
               from UI/deadline          ┌───────────────────┐
                                        │ Terminal + credits│
                                        └─────────┬─────────┘
                                                  ▼
                                        next RUN or safe close
```

Cancellation remains latched across engine startup; close waits for execution and the interrupt coordinator to relinquish the handle (KTD3). Normal replies still flow through owned arenas, not through the cancellation signal.

### Risks and Integration Boundaries

Pane resizing is now the reported merged baseline, not concurrent work awaiting integration. Its affected areas are `panes.c/.h`, window-level size state in `main.c`, shell/sidebar/grid views, UI input cursors, and Explorer's Makefile. Re-read those areas on merged main before implementation; do not copy stale `main.c` or Makefile contents over them. Layout sizes belong to the window and must survive New Session database switches. Robert's successful Mac divider test covers that pane change, not future engine integration. No pane files are modified by this plan.

The first DuckDB implementation checkpoint must prove streaming, cancel-before-first-row, cancel-during-fetch, exact-value display and no-write behavior before UI integration. Deprecated streaming entry points, cold-open I/O and allocations inside DuckDB are the highest technical uncertainties. If the pinned API cannot meet the contract, return with evidence and an alternative; do not silently switch to full materialization or C++ application code.

The existing 1 MiB editor / 127 KiB send bound is preserved, including its refusal message. This feature is not an editor-capacity project. Existing third-party engine code remains outside Safe C's formal proof boundary.

---

## Implementation Units

### U1. Establish the engine boundary without changing PostgreSQL

**Goal:** share execution/result contracts without destabilizing PG. **Requirements:** R3, R4, R6. **Dependencies:** scope and implementation approval.

**Files:** new `adapters/sc_db.h`, `adapters/db.c`; existing `adapters/pq.c`, `adapters/sc_pq.h`; `examples/explorer/{worker,link,links,proto,result,detail}.c/.h`; `tests/explorer_{proto,result,detail,conn}_test.c`, `tests/explorer_pq_integration.c`; corresponding existing protocol/result fuzz harnesses.

**Approach:** first introduce tagged PG dispatch as a behavior-preserving change; verify it before normalizing type/error metadata (KTD1, KTD5). Preserve ownership machines and the libpq async paths. Stage protocol producers and consumers together, because the wire format is private to one binary, not persisted.

**Patterns:** existing `sc_pq` opaque handles and result serialization; `link_keep`/`link_retire` ownership.

**Test scenarios:**
1. Existing PG integration scenarios retain their observable results, including multiple sets, error-after-rows, dropped connections and delayed cancel followed by a successful new query.
2. Numeric, text, unknown PG OID, empty and over-wide column sets either render identically or return the specified query error without poisoning the link.
3. A retained result remains readable after its connection is retired and freed.

**Verification:** both libpq integration builds and existing core tests pass before adding an embedded engine. No new parallel mock suite for behavior already exercised by these tests.

### U2. Add the DuckDB adapter and prove its execution boundary

**Goal:** run bounded, interruptible read-only DuckDB queries through real links. **Requirements:** R1, R3, R5–R7. **Dependencies:** U1.

**Files:** new `adapters/duckdb.c`, `scripts/install-duckdb.sh`, `tests/explorer_duckdb_integration.c`; `adapters/db.c`, `adapters/sc_db.h`, `examples/explorer/worker.c`, `examples/explorer/link.c`, `examples/explorer/Makefile`; targeted new adapter fuzz harness under `fuzz/` and its dependency-aware build target.

**Approach:** implement session ownership, native statement extraction, streaming serialization and interrupt coordination (KTD2–KTD5, KTD8–KTD9). Pin/check the SDK using KTD11. Fixture creation uses disposable writable test handles; the application adapter never opens them writable.

**Test scenarios:**
1. Open a fixture, read an empty set and a large generated set, retain only the allowed rows/chunks, and compare returned cells with independently specified expected values.
2. Cancel before the first row, during fetch, while out of credit, and while replacing/closing the session; every arena returns once and the next query succeeds.
3. Exercise decimal/huge integer, NULL, blob, list/struct and temporal values, including cells above 4 KiB; display/copy never silently loses precision.
4. Missing/wrong-format/locked files fail without creation. Write, COPY, ATTACH, extension and policy-change attempts leave the fixture and sentinel files unchanged.
5. A second metadata connection and a retiring connection share the database instance without premature close; RSS/spill measurements distinguish engine costs from result retention.

**Verification:** real-engine ASan/UBSan and ThreadSanitizer integration evidence, with limits on what prebuilt engine binaries instrument. This is the technical go/no-go checkpoint before proceeding to UI integration.

### U3. Add native SQLite execution and file protection

**Goal:** provide the same execution contract using SQLite semantics. **Requirements:** R1, R3, R5–R7. **Dependencies:** U1; follow U2's settled shared contract.

**Files:** new `adapters/sqlite.c`, `scripts/install-sqlite.sh`, `tests/explorer_sqlite_integration.c`; `adapters/db.c`, `adapters/sc_db.h`, `examples/explorer/Makefile`; targeted adapter fuzz harness under `fuzz/` and its dependency-aware build target.

**Approach:** prepare-tail iteration, worker-owned statements, progress/busy cancellation and native authorizer (KTD3, KTD8–KTD9). Build the pinned amalgamation so sanitizers can instrument the engine too.

**Test scenarios:**
1. Read rollback-journal and live-WAL fixtures, including a committed WAL-only row; never substitute immutable mode or lose journal contents.
2. Existing fixtures with quotes, Unicode or `#` in their filenames open successfully. Missing files, wrong formats, denied permissions and conflicting locks produce bounded, non-password errors without creating a database. An existing zero-byte file follows SQLite's empty-database semantics without being initialized or written.
3. Mixed INTEGER/REAL/TEXT/BLOB/NULL values in one column preserve values, lengths and distinctions across rows and batches.
4. Reject DDL/DML, ATTACH, VACUUM INTO, unsafe PRAGMAs and extension loading while allowing ordinary SELECT and safe introspection.
5. Cancel a recursive query or busy lock wait, close, reconnect, and run again without a stale cancel or leaked statement.

**Verification:** real SQLite integration under ASan/UBSan and ThreadSanitizer, including before/after fixture and sentinel-file checks.

### U4. Add native catalogs, structure and paging

**Goal:** browse both local engines without PG catalog assumptions. **Requirements:** R2, R4–R6. **Dependencies:** U2, U3.

**Files:** `examples/explorer/{catalog,catalog_sql,meta,page,nav,tree}.c/.h`; new `examples/explorer/catalog_duckdb.c`, `examples/explorer/catalog_sqlite.c`; `tests/explorer_{catalog,page,nav,tree}_test.c`, both new integration files, `tests/explorer_pq_integration.c`, `fuzz/explorer_catalog_fuzz.c`.

**Approach:** separate provider SQL/decoding and capabilities, preserving catalog queues and views (KTD6–KTD7). Move PG ownership out of shared code only where local providers require it. Disable unsupported reads rather than queuing fake empty PG responses. Replace timeout SET requests with provider-specific deadline behavior.

**Test scenarios:**
1. Fixtures contain quoted identifiers, two same-named tables in distinct DuckDB schemas, views, composite keys, generated columns, unique indexes and foreign keys; displayed structure matches fixture definitions.
2. SQLite WITHOUT ROWID and nullable composite keys choose the correct ordered/notice paths; no-key DuckDB tables never receive `ctid` SQL.
3. Pages of 1,000, 1,000, 500 and zero rows contain the expected key ranges; count timeout leaves Content usable with unknown total.
4. Reload during metadata/count reads drops stale replies; navigation never resolves an old transient ID to a different object.
5. Missing virtual-table modules, unsupported definitions and definitions over the display-cell cap produce explicit partial/unavailable states, not invented metadata.

**Verification:** each real engine exercises catalog-to-Content/Structure integration; all existing PG catalog/paging scenarios remain green.

### U5. Make editor syntax and execution ranges dialect-aware

**Goal:** the editor sends the SQL Robert selected, not a fragment misidentified by PG token rules. **Requirements:** R3, R4. **Dependencies:** U1; engine integration fixtures from U2/U3 for differential checks.

**Files:** `examples/explorer/{sqltok,sqlkw,editor}.c/.h`, `examples/explorer/editor_syntax.c`, `examples/explorer/main.c`; `tests/explorer_{sqltok,sqlkw,editor}_test.c`, `tests/explorer_sqltok_check.h`, `fuzz/explorer_{sqltok,editor}_fuzz.c`.

**Approach:** extend existing lexical contracts and checkpoints, keeping PG semantics as the default (KTD8). Do not use syntax colors or a SELECT-prefix test as the safety policy.

**Test scenarios:**
1. Semicolons inside SQLite bracket/backtick names and strings, PG dollar/escape strings, and DuckDB strings do not select unintended statements.
2. Nested-comment behavior differs correctly between engines; switching dialect invalidates cached token/statement ranges.
3. A trigger body containing SELECT and CASE/END cannot cause caret execution of an inner statement. An unsupported compound form requires explicit selection/run-all.
4. Selection, run-all, queued edits and caret-only movement preserve their current behavior, including the existing send-size refusal.

**Verification:** unit and differential fuzz evidence against native engine boundary/completeness behavior, plus the tokenizer proof and mutation gates. Preserve the existing independent reference; do not copy implementation logic into its expected output.

### U6. Add the approved file-opening and session UI

**Goal:** use the local engines from the native application without changing its layout. **Requirements:** R1, R3–R5, R8. **Dependencies:** U4, U5; the governing design/wording approval is settled in A4.

**Files:** `examples/explorer/{picker,view_picker,main,links,view_shell,view_history}.c/.h`; `adapters/sc_ui.h`, `adapters/ui_input.c` for native-dialog event handoff; new `tests/explorer_picker_test.c`; existing `tests/explorer_{history,conncfg}_test.c`; local scene fixtures.

**Approach:** implement A4's approved opening flow and exact wording within the existing visual style. Preserve approved A2 and KTD10 persistence scope. Copy native-dialog results into owned adapter storage and deliver a bounded UI event; do not mutate application state in a callback on an arbitrary SDL thread. Dialog cancel leaves the session unchanged. CLI opens use the same tagged target and validation path. Preserve merged pane sizes and drag routing across database switches.

**Test scenarios:**
1. Cancel a file dialog, open each engine, switch through New Session with unsaved SQL, and reconnect a missing file; no unrelated session opens or data is overwritten.
2. A delayed file-dialog result after the picker/session closes is ignored safely.
3. Old PG config/history still load; local history distinguishes equal basenames in different directories and recall only inserts text.
4. Inspect light/dark query, Content, Structure, empty/error and read-only states, plus drag resized panes and exercise neighboring controls.

**Verification:** real Linux SDL screenshots and interaction recording, inspected after capture. Robert separately confirms native macOS file dialogs, fonts, shortcuts, loading and pane interaction. UI implementation does not establish wording approval retroactively.

### U7. Integrate builds, regression gates and adoption documentation

**Goal:** make the three-engine app reproducible and prove PG was not sacrificed. **Requirements:** R4, R7–R9. **Dependencies:** U1–U6.

**Files:** `.agents/setup`, `ci/Dockerfile`, `scripts/ci.py`, `examples/explorer/Makefile`, `examples/explorer/README.md`, `docs/subset-v1.md`, `AGENTS.md`, `LEARNINGS.md`, `examples/explorer/BREAKAGE.md`; existing gate fixtures if the new targets change their contract.

**Approach:** add embedded integration/fuzz targets to manual CI, preserve manual-only workflows, document engine limits and local-read-only meaning, and register the new R0 adapters. Do not put engine headers in public Safe C headers. Record actual dependency digests, platform support and instrumented/uninstrumented coverage. No GitHub workflow dispatch is implied.

**Test scenarios:**
1. Clean Linux build resolves pinned headers/libraries and runs all three engines; core tests and PG-only integration still build without embedded SDKs.
2. Missing or wrong-architecture SDK fails with a useful build error, not silent omission of an engine.
3. Robert builds on his native Mac and opens disposable fixtures for each engine using the documented commands; confirm loader paths outside the repository working directory.

**Verification:** the complete verification contract below is recorded with commands, results and limitations. No delivery or merge without the separate approval.

---

## Verification Contract

Planning did not run builds, tests, installers or database queries. The evidence above is source/document inspection, not proof of a working implementation.

| Check | Required outcome after implementation |
| --- | --- |
| `make check` and `make test` | Application subset lint and existing/new core ASan/UBSan tests pass at each behavioral milestone |
| `make -C examples/explorer pq-test` and `make -C examples/explorer pq-test-18` | Both PG cancel implementations, catalog timeout/retry, paging, kept results, password and reconnect scenarios remain valid |
| Proposed `make -C examples/explorer duckdb-test` / `sqlite-test` | Real files and complete link/worker/protocol paths pass ASan/UBSan and TSan variants; dependency instrumentation limits are reported |
| `make prove PROVE_SRC=examples/explorer/sqltok.c` and `make mutate MUTATE_SRC=examples/explorer/sqltok.c` | All generated goals accepted correctly; non-equivalent tokenizer mutants rejected |
| Existing protocol/result/catalog/editor fuzzing plus new dependency-aware adapter targets | Differential checks cover real parsing/serialization behavior, including stale/cancel/error boundaries; report actual budgets |
| `make -C examples/explorer screenshot` and affected PG/local scenes | Inspect rendered output; record/inspect file-opening and pane/selection interaction, not just static structure |
| File-safety integration probes | Nonexistent paths stay nonexistent; database contents and sentinel files stay unchanged after denied SQL; WAL reads include committed journal data |
| Native macOS acceptance | Robert reports his build and engine-opening/query/cancel/dialog/resize results; Linux success is not a substitute |

Run checks on unchanged PG before U1 and compare after each shared-boundary change. Baseline failures must be distinguished from regressions, not suppressed. Keep existing valuable integration scenarios; add only missing contracts. Use disposable databases and isolated preference directories, never Robert's real files for destructive probes. The 85% coverage goal is a measurement target, not permission to add repetitive tests; report any unavailable coverage measurement honestly.

---

## Definition of Done

- Approved A1–A4 are preserved; implementation and delivery have their own explicit authorization. Any change to approved scope, flow or wording requires Robert's approval.
- Each unit's observable behavior and verification are evidenced, including PostgreSQL preservation and local-file nonmutation checks.
- No retained result or cancellation path accesses a closed engine connection or a returned arena.
- Large-query memory reports distinguish Explorer retention from engine execution and spill; unsupported metadata and SQL features are stated honestly.
- Native Mac acceptance is reported by Robert, or explicitly outstanding rather than presented as complete.
- Temporary experiments, abandoned abstractions and obsolete code paths are removed. Only useful tests and maintained docs remain.
- The final report distinguishes local edits, commits, pushes, PR state and merge state. Nothing in this document preauthorizes those delivery actions.
