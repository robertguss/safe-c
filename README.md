# safe-c

Safe C is planned as a standalone C library that makes C a practical, near-provably safe language for AI agents to write (see [`docs/project-sketch.md`](docs/project-sketch.md)). The standard library is not implemented yet.

## What exists today

- **Subset & rules**: [`AGENTS.md`](AGENTS.md), [`docs/subset-v1.md`](docs/subset-v1.md), and [`waivers.toml`](waivers.toml) (starts empty; expired waivers fail `make gates`). Unsafe C is permitted only in `stdlib/`; other code must stay inside the subset.
- **Lint gate & fixtures**: `scripts/lint.py` and the self-test fixture catalog under `tests/lint/`.
- **Sanitizer & fuzz gates**: `make test`, `make fuzz` (`scripts/fuzz-all.sh`), and `make gate-selftest` with ASan, UBSan, and libFuzzer canaries (`tests/gates/asan-canary.c`, `tests/gates/ubsan-canary.c`, `fuzz/canary_fuzz.c`).
- **Prototype evidence**: Throwaway arena/slice sources and Frama-C WP / lint logs under [`docs/evidence/`](docs/evidence/README.md).

## Make targets

- `make lint`: Scans application C (`*.c` / `*.h`) outside `stdlib/`, `adapters/`, `tests/`, `docs/`, and `research-wiki/` (missing application sources is a pass).
- `make lint-selftest`: Runs the lint fixture catalog under `tests/lint/`.
- `make test`: Builds every `tests/**/*_test.c` with `-fsanitize=address,undefined -fno-sanitize-recover=all` and runs each (passes when there are none).
- `make fuzz`: Runs every `fuzz/*_fuzz.c` with libFuzzer for `FUZZ_SECONDS` (default 60).
- `make gate-selftest`: Builds and runs the ASan, UBSan, and libFuzzer canaries, expecting each to fail and printing `asan canary caught`, `ubsan canary caught`, and `fuzz canary caught`.
- `make gates`: Runs `make lint`, `make test`, and the `waivers.toml` expiry check (`python3 scripts/lint.py --waivers`). Work is done only when `make gates` passes.

## Required tools

`make lint` requires these tools on `PATH` (or `$HOME/.local/bin`), failing closed if `semgrep`, `clang`, or `clang-tidy` is missing (`make test`, `make fuzz`, and `make gate-selftest` also require clang sanitizer/libFuzzer support, using Homebrew `llvm` on macOS when present or via `CC` / `LLVM` overrides):

| Tool | Version |
| --- | --- |
| `python3` | 3.11+ (`tomllib`; tested with 3.12.3) |
| `semgrep` | 1.181.0 |
| `clang` | 18.x (tested with 18.1.3) |
| `clang-tidy` | 18.x (tested with 18.1.3) |

## Planned (not yet implemented)

- **Prover gate (SC-04)**: Frama-C WP verification and ACSL contracts on the standard library joining `make gates`.
