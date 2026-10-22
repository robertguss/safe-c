# Agent rules

Safe C is a standalone C library. Any C project can adopt it.

Work is done only when `make gates` passes. Today that target runs application
lint (`make lint`), sanitizer tests, ThreadSanitizer tests (`make tsan`), sanitizer/fuzzer canaries, short-budget
fuzzing (5 seconds per harness by default), Frama-C WP, the waiver report, and
the TLC check of `models/`.

`make ci` runs everything the Gates workflow runs (`make gates`, the self-tests,
every fixture suite, the HTTP smoke test), in parallel, from the list in
`scripts/ci.py`; `CI_STEPS="sc09 chan"` picks steps, `CI_JOBS` caps concurrency,
and logs land in `build/ci/`. `make ci-docker` runs the same thing inside the
pinned toolchain from `ci/Dockerfile`, so a machine with only Docker needs no
other install. Run one of them green before pushing.

qgate (`qgate.toml`, `qgate.baseline`) runs as git hooks once a clone has run `qgate install`:
commit runs `scripts/lint.py` when C changes, plus qgate's function length, file length and
duplication checks; push also runs the ASan/UBSan and ThreadSanitizer tests. The baseline only
shrinks. A failure prints its fix; anything that loosens the gate (a raised limit, a grown
baseline, an ignore marker) shows as a `loosening` warning to raise with Robert, and never bypass
the hooks. The Gates workflow's `qgate` job runs `qgate run --stage push --base <base>` on every
PR and push to `main`, at the version `qgate.toml` pins, so a change that skipped the hooks still
fails there. qgate does not replace `make ci`, and `make ci` does not run qgate.

`make lint` scans application C (`*.c` / `*.h`) outside `stdlib/`, `adapters/`,
`tests/`, `fuzz/`, `docs/`, and `research-wiki/`. Missing application sources is a pass.
`make lint-selftest` is the fixture catalog under `tests/lint/`; it is not the
application gate.
`make install PREFIX=...` installs `libsafec.a`, the headers, `safec.pc` and the `safec-lint` rule pack; `make package-test` checks them from outside the tree (CI: `.github/workflows/adopter.yml`, which also runs the reusable `safec-adopter.yml` on `examples/adopter/`). `make gates` skips both.
`make guide-test` compiles and lints every code example in `docs/guide/` and `docs/agent-prompt.md` and runs the quickstart (`scripts/guide-check.py`); an example in those docs needs a tag, and CI runs it as the `guide` step. `make gates` skips it.
`make test` links the standard library and runs every `*_test.c` under `tests/`
except `tests/lint/` and `tests/gates/` with
`-fsanitize=address,undefined -fno-sanitize-recover=all` (passes when none exist).
`make fuzz` runs every `fuzz/*_fuzz.c` with libFuzzer for `FUZZ_SECONDS` (default 60);
per-input timeout matches that budget.
`make gate-selftest` verifies ASan, UBSan, libFuzzer, and WP canaries, a matching
proof waiver, and an expired waiver. `make prove` checks `stdlib/*.c` and writes
`build/prove.log`; `PROVE_SRC` selects explicit files for gate fixtures.
`make mutate` plants a fixed set of code mutations (flipped or relaxed comparisons, `+`/`-` swaps,
swapped `return true/false`, an `if` condition forced to 0, each `||`/`&&` clause dropped) in every
file `make prove` checks and requires WP to reject each one; a surviving mutant is a weak contract
and fails the target. A mutant that cannot compile is discarded. `MUTATE_SRC` selects explicit files,
`MUTATE_JOBS` the parallelism, `MUTATE_SHARD=K/N` runs only every Nth mutant starting at the
Kth, and a survivor that changes no behavior goes in
`tests/mutate-equivalent.toml` with a reason. It took about 27 minutes on a 4-core CI runner (165 mutants), so
`make gates` skips it and CI splits it across six `mutate (K/6)` jobs; the `gates` job passes only
when the other checks and every shard pass. `make ci` still runs it whole.
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
`make gates` skips all four; CI runs them as ci.py steps.
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
harness, except the channel and mailbox modules, whose headers document their contracts in prose. The arena, slice, tslice, string, result, and handle modules are in this tree. The
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

`adapters/posix.c` is the errno adapter: `open`, `read`, `fstat`, `close` and `sysconf` return `sc_result_size`
with the errno in `err`, so application code never reads `errno` (rule R9). Adapters are not in `libsafec.a`;
`tests/posix_test.c` and `fuzz/posix_fuzz.c` include `adapters/posix.c` directly. Calls into libc are outside WP, so
the adapter has tests and a fuzz harness and no ACSL contracts.

`examples/http/` is the HTTP demo: application C inside the subset, with its one
libmicrohttpd dependency in `adapters/microhttpd.c` (rule R0). `make lint` scans it,
`tests/http_parse_test.c` and `fuzz/http_parse_fuzz.c` cover its parser, and CI builds
and smoke-tests the server with `make -C examples/http smoke`; `make gates` does not
build it. Lint compiles with `-Iinclude -Iadapters`.

A proof gap is allowed only as a waiver in `waivers.toml`. A waiver names an
id, a file, a function, a goal, a reason, an owner, and an expiry date. An
expired waiver fails `make gates`. The file starts with no waiver entries.

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
