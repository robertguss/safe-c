# Agent rules

Safe C is a standalone C library. Any C project can adopt it.

Work is done only when `make gates` passes. Today that target runs application
lint (`make lint`), sanitizer tests, sanitizer/fuzzer canaries, short-budget
fuzzing (5 seconds per harness by default), and the waiver expiry check.
Frama-C WP joins in SC-04.

`make lint` scans application C (`*.c` / `*.h`) outside `stdlib/`, `adapters/`,
`tests/`, `fuzz/`, `docs/`, and `research-wiki/`. Missing application sources is a pass.
`make lint-selftest` is the fixture catalog under `tests/lint/`; it is not the
application gate.
`make test` builds and runs every `tests/**/*_test.c` with
`-fsanitize=address,undefined -fno-sanitize-recover=all` (passes when none exist).
`make fuzz` runs every `fuzz/*_fuzz.c` with libFuzzer for `FUZZ_SECONDS` (default 60).
`make gate-selftest` verifies that ASan, UBSan, and libFuzzer canaries are caught.
`FUZZ_SECONDS` overrides either fuzz budget and must be a positive integer.
`scripts/sanitize.py` owns compiler selection and sanitizer flags. `CC`, `LLVM`,
and `CFLAGS` overrides apply to tests and fuzzing without changing lint's PATH.
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
harness. Those artifacts are not in this tree yet.

Unsafe C lives only in `stdlib/`. Other code stays inside the subset in
`docs/subset-v1.md`.

A proof gap is allowed only as a waiver in `waivers.toml`. A waiver names an
id, a file, a function, a goal, a reason, an owner, and an expiry date. An
expired waiver fails `make gates`. The file starts with no waiver entries.
