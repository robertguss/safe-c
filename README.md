# safe-c

Safe C is planned as a standalone C library that makes C a practical, near-provably safe language for AI agents to write (see [`docs/project-sketch.md`](docs/project-sketch.md)). The standard library is not implemented yet.

## What exists today

- **Subset & rules**: [`AGENTS.md`](AGENTS.md), [`docs/subset-v1.md`](docs/subset-v1.md), and [`waivers.toml`](waivers.toml) (starts empty; expired waivers fail `make gates`). Unsafe C is permitted only in `stdlib/`; other code must stay inside the subset.
- **Lint gate & fixtures**: `scripts/lint.py` and the self-test fixture catalog under `tests/lint/`.
- **Sanitizer & fuzz gates**: `scripts/sanitize.py` backs `make test`, `make fuzz`, and `make gate-selftest`, with ASan, UBSan, and libFuzzer canaries under `tests/gates/`. Shared toolchain helpers live in `scripts/tools.py`.
- **Proof gate & CI**: Frama-C WP checks `stdlib/*.c`; explicit waivers and proof logs accompany the gate. GitHub Actions runs gates and fixture suites on PRs, with a longer nightly fuzz budget. Valgrind and the Clang Static Analyzer are advisory.
- **Prototype evidence**: Throwaway arena/slice sources and Frama-C WP / lint logs under [`docs/evidence/`](docs/evidence/README.md).

## Make targets

- `make lint`: Scans application C (`*.c` / `*.h`) outside `stdlib/`, `adapters/`, `tests/`, `fuzz/`, `docs/`, and `research-wiki/` (missing application sources is a pass).
- `make lint-selftest`: Runs the lint fixture catalog under `tests/lint/`.
- `make test`: Builds every `*_test.c` under `tests/` except `tests/lint/` and `tests/gates/` with `-fsanitize=address,undefined -fno-sanitize-recover=all` and runs each (passes when there are none).
- `make fuzz`: Runs every `fuzz/*_fuzz.c` with libFuzzer for `FUZZ_SECONDS` (default 60). Per-input `-timeout` matches that budget so a hung harness cannot stall for 20 minutes.
- `make gate-selftest`: Checks sanitizer and fuzzer canaries, then checks that the WP canary fails, its exact waiver passes, and an expired waiver fails.
- `make prove`: Runs WP with RTE and unsigned-overflow checks on `stdlib/*.c`, writing `build/prove.log`. Set `PROVE_SRC=tests/gates/wp-canary.c` to exercise the deliberate failure.
- `make waivers`: Prints the complete waiver register and fails on invalid or expired entries.
- `make gates`: Runs application lint, sanitizer tests, canaries, production fuzzing (5 seconds per harness by default), then Frama-C WP with waiver validation. Work is done only when `make gates` passes.

`FUZZ_SECONDS` overrides either fuzz budget and must be positive; zero means unlimited to libFuzzer and is rejected. Production harnesses belong in `fuzz/`; deliberately failing canaries belong in `tests/gates/`. Run wrapper regression tests with `python3 tests/gates/runner_test.py` and proof-boundary tests with `python3 tests/gates/proof_test.py`.

## Required tools

`make lint` requires these tools on `PATH` (or `$HOME/.local/bin`), failing closed if `semgrep`, `clang`, or `clang-tidy` is missing (`make test`, `make fuzz`, and `make gate-selftest` also require clang sanitizer/libFuzzer support, using Homebrew `llvm` on macOS when present or via `CC` / `LLVM` overrides):

| Tool | Version |
| --- | --- |
| `python3` | 3.11+ (`tomllib`; tested with 3.12.3) |
| `semgrep` | 1.181.0 |
| `clang` | 18.x (tested with 18.1.3) |
| `clang-tidy` | 18.x (tested with 18.1.3) |

Sanitizer builds honor `CC`, `LLVM`, and `CFLAGS` without changing lint tool selection. On Linux, install compiler-rt, libFuzzer, and the GCC C++ development libraries (`g++`). The wrapper adds the library directory reported by `g++ -print-file-name=libstdc++.so`; `LIBRARY_PATH` can supply it when needed. On macOS, put the intended clang and clang-tidy on PATH explicitly when running lint; sanitizer builds can find Homebrew LLVM independently.

## Proof toolchain and waivers

CI pins OCaml 5.2.1, Frama-C 33.0, Alt-Ergo 2.6.4, and Why3 1.8.2 through opam. Z3 4.13.3 comes from the [Z3 project's binary package](https://pypi.org/project/z3-solver/4.13.3.0/), `z3-solver==4.13.3.0`, avoiding a C++ rebuild on cold runners. To reproduce locally, install those packages, put both bin directories on PATH, and run `why3 config detect`. `make prove` requires Frama-C, Alt-Ergo, and Z3 even when there are no standard-library sources.

The runner reads Frama-C's per-goal JSON report rather than its exit code or the printed N-of-N total. Each unproved goal needs an exact `file`, `function`, and `goal` match in `waivers.toml`, plus `id`, `reason`, `owner`, and an unexpired `expires` date. Empty reports, missing reports, tool errors, and other proof warnings fail the gate. The production register starts empty.

Frama-C 33 reports that alignment and function-pointer RTE guards are skipped. The gate records these as `wp-rte-unaligned` and `wp-rte-function-pointer`, with function `<translation-unit>` and the analyzed source file. These gaps require explicit waivers, including when all generated goals pass. See the [Frama-C WP manual](https://frama-c.com/download/frama-c-wp-manual.pdf) for the model and report limitations. The synthetic waivers used by `gate-selftest` exist only in temporary files.

CI uploads `build/prove.log`, `build/prove-selftest.log`, `build/gates.log`, and the measured opam installation time in `build/ci-times.txt`. The opam cache makes cold and warm timings distinguishable. Nightly runs reuse the gates workflow with 1800 seconds per production fuzz harness. There are no production standard-library modules yet; the proof canary verifies gate behavior, not library safety.
