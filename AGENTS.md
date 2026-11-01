# Agent rules

Safe C is a standalone C library. Any C project can adopt it.

Nothing runs automatically: no check runs on push, pull request or a schedule, and
no check has to pass before pushing. Before calling a change done, run `make check`
(application lint, then the sanitizer unit tests; about 30 seconds). Run the heavier
checks below when Robert asks or when the change calls for them, and name in the PR
body which checks ran and which did not.

| The change touches | Also run |
| --- | --- |
| A lint rule or `tests/lint/` | `make lint-selftest` |
| An ACSL contract or a proved function in `stdlib/` | `make prove PROVE_SRC=<file>`, then `make properties` and `make mutate MUTATE_SRC=<file>` |
| `examples/explorer/sqltok.c` | `make prove PROVE_SRC=examples/explorer/sqltok.c` (`make ci CI_STEPS=explorer-prove`), then `make mutate MUTATE_SRC=examples/explorer/sqltok.c` |
| `stdlib/chan.c` or `stdlib/mailbox.c` | `make tsan`, then `make model`, `make genmc` and `make ci CI_STEPS="chan mailbox"` |
| `models/` | `make model` and `make tlaps` |
| `docs/guide/` or `docs/agent-prompt.md` | `make guide-test` |
| Install, `safec.pc` or the rule pack | `make package-test` |
| `examples/explorer/`, `adapters/ui.c`, `adapters/ui_input.c`, `scripts/install-sdl3.sh` or `.agents/setup` | `make -C examples/explorer screenshot`, then inspect the PNG |
| Explorer core (`examples/explorer/*.c` built by `tests/explorer_*_test.c`) | `make test` |
| `adapters/thread.c` | `make test` and `make tsan` |
| `adapters/mem.c` | `make test` |
| `adapters/pq.c` or the explorer's worker (`worker.c`, `link.c`, `wire.c`) | `make -C examples/explorer pq-test` and `make -C examples/explorer pq-test-18` |
| `adapters/db.c`, `adapters/db_local.c`, `adapters/duckdb.c`, `adapters/duckdb_format.c`, `adapters/sqlite.c`, their headers or Explorer's local catalog providers | `make -C examples/explorer duckdb-test sqlite-test`, then `make -C examples/explorer duckdb-fuzz sqlite-fuzz FUZZ_SECONDS=20` |
| `scripts/install-libpq.sh` | `sh scripts/install-libpq.sh`, then `make -C examples/explorer pq-test-18` |
| `scripts/install-duckdb.sh` or `scripts/install-sqlite.sh` | that installer, then its `duckdb-test` or `sqlite-test` target |
| A gate script or fixture under `tests/gates/` | that fixture's `make ci CI_STEPS=<step>` (`python3 scripts/ci.py --list` names them) |
| Everything | `make gates`, `make ci` or `make ci-docker` |

`make gates` runs application lint (`make lint`), sanitizer tests, ThreadSanitizer
tests (`make tsan`), sanitizer/fuzzer canaries, short-budget fuzzing (5 seconds per
harness by default), Frama-C WP, the property clients (`make properties`), the waiver
report, and the TLC check of `models/`.

`make ci` runs every check (`make gates`, the self-tests, every fixture suite, the HTTP
smoke test, mutation and the formal tools), in parallel, from the list in
`scripts/ci.py`; `CI_STEPS="sc09 chan"` picks steps, `CI_JOBS` caps concurrency,
and logs land in `build/ci/`. `make ci-docker` runs the same thing inside the
pinned toolchain from `ci/Dockerfile`, so a machine with only Docker needs no
other install. It pulls the image the CI image workflow publishes for the current
Dockerfile, which refreshes only when someone runs that workflow by hand on `main`;
otherwise it builds the image locally. The GitHub workflows run only when started
by hand from the Actions tab: Gates runs `make ci` with mutate split across six
jobs; Adopter runs `make package-test` and the sample adopter; CI image builds
and publishes `ci/Dockerfile` and runs `make ci` inside it; Fuzz (long) runs each
harness in its own job (1800 seconds by default).

`make lint` scans application C (`*.c` / `*.h`) outside `stdlib/`, `adapters/`,
`tests/`, `fuzz/`, `docs/`, and `research-wiki/`. Missing application sources is a pass.
`make lint-selftest` is the fixture catalog under `tests/lint/`; it is not the
application gate.
`make install PREFIX=...` installs `libsafec.a`, the headers, `safec.pc` and the `safec-lint` rule pack; `make package-test` checks them from outside the tree (manual workflow: `.github/workflows/adopter.yml`, which also runs the reusable `safec-adopter.yml` on `examples/adopter/`). `make gates` skips both.
`make guide-test` compiles and lints every code example in `docs/guide/` and `docs/agent-prompt.md` and runs the quickstart (`scripts/guide-check.py`); an example in those docs needs a tag, and `make ci` runs it as the `guide` step. `make gates` skips it.
`make test` links the standard library and runs every `*_test.c` under `tests/`
except `tests/lint/` and `tests/gates/` with
`-fsanitize=address,undefined -fno-sanitize-recover=all` (passes when none exist).
`make fuzz` runs every `fuzz/*_fuzz.c` with libFuzzer for `FUZZ_SECONDS` (default 60);
per-input timeout matches that budget.
`make gate-selftest` verifies ASan, UBSan, libFuzzer, and WP canaries, a matching
proof waiver, and an expired waiver. `make prove` checks `stdlib/*.c` and writes
`build/prove.log`; `PROVE_SRC` selects explicit files for gate fixtures. `make prove` and `make gates`
cover `stdlib/*.c` only; the proved app file `examples/explorer/sqltok.c` is proved by the
`explorer-prove` step of `make ci`, mutated by plain `make mutate`, and has no blind properties.
`make mutate` plants a fixed set of code mutations (flipped or relaxed comparisons, `+`/`-` swaps,
swapped `return true/false`, an `if` condition forced to 0, each `||`/`&&` clause dropped) in every
file `make prove` checks, plus `examples/explorer/sqltok.c`, and requires WP to reject each one; a surviving mutant is a weak contract
and fails the target. The baseline proves the whole file; each mutant proves only the function it changed
(`prove.py --function`), since callers see its unchanged contract. A mutant that cannot compile is discarded. `MUTATE_SRC` selects explicit files,
`MUTATE_JOBS` the parallelism, `MUTATE_SHARD=K/N` runs only every Nth mutant starting at the
Kth, and a survivor that changes no behavior goes in
`tests/mutate-equivalent.toml` with a reason. The stdlib mutants took about 27 minutes on a 4-core CI
runner before sqltok.c joined them (`python3 scripts/mutate.py --list` counts the plan). So
`make gates` skips it, `make ci` runs it whole, and the manual Gates workflow splits it across six
`mutate (K/6)` jobs. `MUTATE_SRC=<file>` on the file you changed is much shorter.
`make properties` proves each module's property clients in `tests/properties/<module>/props.c` from the
callee contracts alone, with `-wp-smoke-tests` and no `-wp-rte`, and never reads `waivers.toml`. A separate
model wrote them from the task brief with `scripts/blind.py`, seeing only the brief and the public headers
stripped of every function contract and comment; the manifest's hashes make a hand edit or a changed
declaration fail as stale. Only `blind.py` changes a property: a code agent that thinks one is wrong runs
`blind.py dispute`. `tests/gates/properties_test.py` weakens one `ensures` per module and requires a red run.
`make waivers` prints every waiver and rejects expired or malformed entries.
`make model` runs TLC on `models/channel.tla` at each queue size in
`MODEL_QUEUE_SIZES` (default `1 3`), and on `models/mailbox.tla` at each sender count in
`MODEL_SENDERS` (default `2 3`) and each of those queue sizes. It needs Java 11+ and `tla2tools.jar` 1.7.4,
pinned by SHA-256 in `scripts/model.py`, at `$TLA2TOOLS`,
`build/tools/tla2tools.jar` or `~/.local/lib/tla2tools.jar`; `models/README.md` maps each action to its C function.
`make tlaps` checks the TLAPS proofs `models/channel_proofs.tla` and `models/mailbox_proofs.tla`, which prove
every safety invariant of both models for any queue size and sender count (TLC still owns the liveness
property `Drains`). It needs tlapm 1.6.0-pre, pinned by SHA-256 in `scripts/install-tlapm.sh`, at `$TLAPM`,
`build/tools/tlapm/bin/tlapm` or on `PATH`.
`make genmc` runs GenMC 0.19.0 on the harnesses in `tests/genmc/`, which include the real `stdlib/chan.c` and
`stdlib/mailbox.c` and assert ownership, order and no-loss; GenMC explores every execution the C11 memory
model allows at the small sizes listed in `scripts/genmc.py`, and a race on a non-atomic location fails too.
`scripts/install-genmc.sh` builds it (LLVM 20, g++ 14) into `build/tools/genmc`; `$GENMC` or `PATH` also work.
`tests/gates/genmc_test.py` and `tests/models/tlaps_test.py` plant bugs and require each tool to go red.
`make gates` skips all four; `make ci` runs them as ci.py steps.
`FUZZ_SECONDS` overrides either fuzz budget and must be a positive integer.
`make bench` builds `tests/bench/*_bench.c` at `-O2` without sanitizers and
prints latency; perf numbers are recorded, not gated, and `make gates` skips it.
`scripts/sanitize.py` owns compiler selection and sanitizer flags. Shared paths
and tool lookup live in `scripts/tools.py`. `CC`, `LLVM`, and `CFLAGS` overrides
apply to tests and fuzzing without changing lint's PATH.
On Linux, install compiler-rt, libFuzzer, and GCC C++ development libraries;
the wrapper queries `g++` for the directory containing `libstdc++.so`.

Lint requires these tools on `PATH` (or `$HOME/.local/bin`):

| Tool | Version used here |
| --- | --- |
| python3 | 3.11+ (`tomllib`; this tree ran 3.12.3) |
| semgrep | 1.181.0 |
| clang | 18.x (this tree ran 18.1.3) |
| clang-tidy | 18.x (this tree ran 18.1.3) |

Missing `semgrep`, `clang`, or `clang-tidy` fails closed with an error that names
the tool.

Every public function in the standard library has an ACSL contract and a fuzz
harness, except the channel and mailbox modules, whose headers document their contracts in prose. The arena, slice, tslice, string, result, handle, buf and utf8 modules are in this tree. The
channel module (`stdlib/chan.c`, one sender) and the mailbox module
(`stdlib/mailbox.c`, many senders, one receiver) are the exception to the proof
rule: threads and atomics are outside WP, so `make prove` skips both with no
waiver and tests, ThreadSanitizer (`make tsan` runs every test that includes
`<pthread.h>`), a fuzz harness and the TLA+ models (`models/channel.tla`,
`models/mailbox.tla`) are their evidence, with GenMC (`make genmc`) on the C and TLAPS
(`make tlaps`) on the models. `tests/gates/chan_test.py` and
`tests/gates/mailbox_test.py` plant bugs in each and require a red result.

Unsafe C lives only in `stdlib/`. Other code stays inside the subset in
`docs/subset-v1.md`.

`adapters/posix.c` is the errno adapter: `open`, `read`, `write`, `fsync`, `rename`, `unlink`, `fstat`, `close` and `sysconf` return `sc_result_size`
with the errno in `err`, so application code never reads `errno` (rule R9). Adapters are not in `libsafec.a`;
`tests/posix_test.c` and `fuzz/posix_fuzz.c` include `adapters/posix.c` directly. Calls into libc are outside WP, so
the adapter has tests and a fuzz harness and no ACSL contracts.

`adapters/thread.c` (pthreads start and join with a stack size, `sc_wake` self-pipe, `sc_flag` atomic flag,
monotonic and Unix clocks) and `adapters/mem.c` (one `mmap`'d region carved into a pool of arenas handed out by index, and volatile secret erasure)
are the explorer's thread and memory adapters, with errno in `err` like `adapters/posix.c`. `tests/thread_test.c`
includes `<pthread.h>`, so `make tsan` runs it; `tests/mem_test.c`, `fuzz/thread_fuzz.c` and `fuzz/mem_fuzz.c`
include the adapters directly.

`examples/http/` is the HTTP demo: application C inside the subset, with its one
libmicrohttpd dependency in `adapters/microhttpd.c` (rule R0). `make lint` scans it,
`tests/http_parse_test.c` and `fuzz/http_parse_fuzz.c` cover its parser, and
`make -C examples/http smoke` (a `make ci` step) builds and smoke-tests the server; `make gates` does not
build it. Lint compiles with `-Iinclude -Iadapters`.

`examples/explorer/` is the PostgreSQL/DuckDB/SQLite explorer, a Postico-style app that tests Safe C on
a real program (`docs/plans/2026-10-28-1032-feat-explorer-local-engines-plan.md`). It is
application C inside the subset and `make lint` scans it. It needs SDL3, SDL3_ttf
and libpq through `pkg-config`; on Linux `scripts/install-sdl3.sh` builds the pinned
SDL3 into `build/tools/sdl3`, `scripts/install-libpq.sh` builds only libpq from the
pinned PostgreSQL 18 source into `build/tools/libpq18`, and `.agents/setup` prepares a
fresh orb (clang 18, semgrep, Postgres, SDL3, libpq 18, DuckDB and SQLite). When libpq 18 is there the
explorer links it, with an rpath, so Linux runs the Mac's libpq 17+ cancel path (the non-blocking cancel; rows use single-row mode on every libpq). `make -C examples/explorer` (the `explorer-build` step of
`make ci`) builds it; `make gates` does not. `adapters/ui.c` and `adapters/ui_input.c` are its R0 adapter
for SDL3, SDL3_ttf and Clay (vendored at `adapters/third_party/`); `ui.c` owns the C `main`.
`adapters/pq.c` is its libpq adapter, called only from worker threads.
`make -C examples/explorer pq-test` (the `explorer-pq` step of `make ci`) runs
`tests/explorer_pq_integration.c` against a temporary Postgres server
(`examples/explorer/pgtest.sh`) under ASan and UBSan, then ThreadSanitizer, with the
system libpq; `make -C examples/explorer pq-test-18` (the `explorer-pq18` step) runs it
again against libpq 18.
`scripts/install-duckdb.sh` installs the checked DuckDB 2.0.0 C SDK for macOS/Linux
x86-64/arm64 into `build/tools/duckdb`; `scripts/install-sqlite.sh` builds the checked
SQLite 3.54.1 amalgamation into `build/tools/sqlite`. Both are private Explorer dependencies,
not dependencies of `libsafec.a` or pure-core tests. `adapters/db.c` dispatches the three engines;
the local adapters open existing files read-only and disable external access/extensions.
`duckdb-test` and `sqlite-test` exercise real files under ASan/UBSan then TSan; the
SQLite source is instrumented, but the prebuilt DuckDB engine is not. `duckdb-fuzz` and
`sqlite-fuzz` compare native results and statement ranges. Manual `make ci` includes them
as `explorer-duckdb`, `explorer-sqlite` and `explorer-local-fuzz`; its image uses
`DUCKDB=/opt/duckdb` and `SQLITE=/opt/sqlite`. No check runs automatically.
One database is shared by all window tabs. Pane sizes stay window-owned across New Session.
Linux checks are not native macOS acceptance; Robert performs that separately.
`make -C examples/explorer screenshot SCENE=<name>` renders a scene headlessly to
`build/explorer/<name>.png`. Log every Safe C gap the app runs into in
`examples/explorer/BREAKAGE.md`.

A proof gap is allowed only as a waiver in `waivers.toml`. A waiver names an
id, a file, a function, a goal, a reason, an owner, and an expiry date. An
expired waiver fails `make gates`.

Proofs require Frama-C 33.0, Alt-Ergo 2.6.4, and Z3 4.13.3 on PATH. CI uses
OCaml 5.2.1 and Why3 1.8.2. A generated WP goal is accepted only when valid or
matched exactly by waiver file, function, and goal. Proof warnings fail closed.
Frama-C's omitted alignment and function-pointer RTE guards are explicit goals
`wp-rte-unaligned` and `wp-rte-function-pointer` with function
`<translation-unit>` for the analyzed file. They require their own waivers;
N-of-N generated goals alone is not proof of full RTE coverage.

Workflow guides live in `.claude/skills/<name>/SKILL.md`. Read the matching
one before the work: `safe-c-conventions` before writing stdlib C or ACSL,
`safe-c-testing` for tests, harnesses, and red gates, `safe-c-review` before
opening a PR, `safe-c-stack-pr` and `safe-c-stack-landing` for the PR stack,
`safe-c-notion-sync` for the Notion hub, and `research-wiki` for the wiki.
