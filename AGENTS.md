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

`make lint` scans application C (`*.c` / `*.h`) outside `stdlib/`, `adapters/`,
`tests/`, `fuzz/`, `docs/`, and `research-wiki/`. Missing application sources is a pass.
`make lint-selftest` is the fixture catalog under `tests/lint/`; it is not the
application gate.
`make install PREFIX=...` installs `libsafec.a`, the headers, `safec.pc` and the `safec-lint` rule pack; `make package-test` checks them from outside the tree (CI: `.github/workflows/adopter.yml`, which also runs the reusable `safec-adopter.yml` on `examples/adopter/`). `make gates` skips both.
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
`MUTATE_JOBS` the parallelism, and a survivor that changes no behavior goes in
`tests/mutate-equivalent.toml` with a reason. It took about 27 minutes on a 4-core CI runner (165 mutants), so CI runs it as its own step
and `make gates` skips it.
`make waivers` prints every waiver and rejects expired or malformed entries.
`make model` runs TLC on `models/channel.tla` at each queue size in
`MODEL_QUEUE_SIZES` (default `1 3`). It needs Java 11+ and `tla2tools.jar` 1.7.4,
pinned by SHA-256 in `scripts/model.py`, at `$TLA2TOOLS`,
`build/tools/tla2tools.jar` or `~/.local/lib/tla2tools.jar`; `models/README.md` maps each action to its C function.
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
harness, except the channel module, whose header documents its contracts in prose. The arena, slice, string, result, and handle modules are in this tree. The
channel module (`stdlib/chan.c`) is the exception to the proof rule: threads and
atomics are outside WP, so `make prove` skips it and tests, ThreadSanitizer
(`make tsan` runs every test that includes `<pthread.h>`) and the TLA+ model
are its evidence.

Unsafe C lives only in `stdlib/`. Other code stays inside the subset in
`docs/subset-v1.md`.

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
