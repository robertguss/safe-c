# safe-c

Safe C is planned as a standalone C library that makes C a practical, near-provably safe language for AI agents to write (see [`docs/project-sketch.md`](docs/project-sketch.md)). The standard library is not implemented yet.

## What exists today

- **Subset & rules**: [`AGENTS.md`](AGENTS.md), [`docs/subset-v1.md`](docs/subset-v1.md), and [`waivers.toml`](waivers.toml) (starts empty; expired waivers fail `make gates`). Unsafe C is permitted only in `stdlib/`; other code must stay inside the subset.
- **Lint gate & fixtures**: `scripts/lint.py` and the self-test fixture catalog under `tests/lint/`.
- **Sanitizer & fuzz gates**: `scripts/sanitize.py` backs `make test`, `make fuzz`, and `make gate-selftest`, with ASan, UBSan, and libFuzzer canaries under `tests/gates/`. Shared toolchain helpers live in `scripts/tools.py`.
- **Prototype evidence**: Throwaway arena/slice sources and Frama-C WP / lint logs under [`docs/evidence/`](docs/evidence/README.md).

## Make targets

- `make lint`: Scans application C (`*.c` / `*.h`) outside `stdlib/`, `adapters/`, `tests/`, `fuzz/`, `docs/`, and `research-wiki/` (missing application sources is a pass).
- `make lint-selftest`: Runs the lint fixture catalog under `tests/lint/`.
- `make test`: Builds every `*_test.c` under `tests/` except `tests/lint/` and `tests/gates/` with `-fsanitize=address,undefined -fno-sanitize-recover=all` and runs each (passes when there are none).
- `make fuzz`: Runs every `fuzz/*_fuzz.c` with libFuzzer for `FUZZ_SECONDS` (default 60). Per-input `-timeout` matches that budget so a hung harness cannot stall for 20 minutes.
- `make gate-selftest`: Builds and runs the ASan, UBSan, and libFuzzer canaries, expecting each to fail and printing `asan canary caught`, `ubsan canary caught`, and `fuzz canary caught`.
- `make gates`: Runs application lint, sanitizer tests, canaries, production fuzzing (5 seconds per harness by default), and the `waivers.toml` expiry check, in that order. Work is done only when `make gates` passes.

`FUZZ_SECONDS` overrides either fuzz budget and must be positive; zero means unlimited to libFuzzer and is rejected. Production harnesses belong in `fuzz/`; deliberately failing canaries belong in `tests/gates/`. Run wrapper regression tests with `python3 tests/gates/runner_test.py`.

## Required tools

`make lint` requires these tools on `PATH` (or `$HOME/.local/bin`), failing closed if `semgrep`, `clang`, or `clang-tidy` is missing (`make test`, `make fuzz`, and `make gate-selftest` also require clang sanitizer/libFuzzer support, using Homebrew `llvm` on macOS when present or via `CC` / `LLVM` overrides):

| Tool | Version |
| --- | --- |
| `python3` | 3.11+ (`tomllib`; tested with 3.12.3) |
| `semgrep` | 1.181.0 |
| `clang` | 18.x (tested with 18.1.3) |
| `clang-tidy` | 18.x (tested with 18.1.3) |

Sanitizer builds honor `CC`, `LLVM`, and `CFLAGS` without changing lint tool selection. On Linux, install compiler-rt, libFuzzer, and the GCC C++ development libraries (`g++`). The wrapper adds the library directory reported by `g++ -print-file-name=libstdc++.so`; `LIBRARY_PATH` can supply it when needed. On macOS, put the intended clang and clang-tidy on PATH explicitly when running lint; sanitizer builds can find Homebrew LLVM independently.

## Planned (not yet implemented)

- **Prover gate (SC-04)**: Frama-C WP verification and ACSL contracts on the standard library joining `make gates`.
