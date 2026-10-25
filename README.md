# safe-c

Version 0.1.0 ([changelog](CHANGELOG.md)).

Safe C is a standalone C library that makes C a practical, near-provably safe language for AI agents to write: a strict subset, a safe standard library, and gates that check both (see [`docs/project-sketch.md`](docs/project-sketch.md)). Any C project can adopt it.

## What exists today

- **Subset & rules**: [`AGENTS.md`](AGENTS.md), [`docs/subset-v1.md`](docs/subset-v1.md) (rules R0 to R14), and [`waivers.toml`](waivers.toml). Unsafe C is permitted only in `stdlib/`; other code must stay inside the subset.
- **Standard library** (`include/safec/`, `stdlib/`): `arena` (bump allocator with an epoch), `slice` (checked pointer and length views), `tslice` (checked arrays of integers or structs, `SC_TSLICE_GET`/`SET`), `str` (byte-slice strings), `result` (`Result` and `Option` tagged structs; lint rule R13 flags reading `.value` without a tag check), `handle` (generation-checked handles) `chan` (bounded ownership-passing channel, one sender) and `mailbox` (`sc_mbox`, many senders, one receiver). Every module except `chan` and `mailbox` has ACSL contracts proved by Frama-C WP. Those two are outside WP: tests, ThreadSanitizer and the TLA+ models are their evidence, and `make prove` skips them with no waiver.
- **Gates**: lint (`scripts/lint.py`, fixtures under `tests/lint/`), ASan/UBSan tests, ThreadSanitizer tests, sanitizer and fuzzer canaries, a libFuzzer harness per module in `fuzz/` (plus one for the HTTP parser), Frama-C WP, the property clients, the waiver report, and TLC on [`models/`](models/README.md). `scripts/sanitize.py` and `scripts/tools.py` own compiler selection and tool lookup.
- **HTTP demo**: [`examples/http/`](examples/http/README.md) serves `GET /` with one arena per request. Application C stays inside the subset; its one libmicrohttpd dependency is `adapters/microhttpd.c` (rule R0). `make gates` does not build it; `make -C examples/http smoke` (a `make ci` step) builds and smoke-tests it.
- **CI**: nothing runs automatically. `make check` (lint plus sanitizer tests) is the default before calling a change done; `AGENTS.md` lists which heavier check fits which change, and `make ci` runs all of them. The GitHub workflows run only when started by hand: Gates runs `make ci` with mutate split across six jobs, and Fuzz (long) gives each harness its own job (1800 seconds by default, corpus kept between runs; see [Replaying a fuzz crash](docs/guide/red-gates.md#replaying-a-fuzz-crash)). Valgrind and the Clang Static Analyzer are advisory.
- **Evidence and research**: prototype and per-task logs under [`docs/evidence/`](docs/evidence/README.md), and the research wiki in [`research-wiki/`](research-wiki/SCHEMA.md).
- **Workflow skills**: `.claude/skills/`, listed in `AGENTS.md`.
- **Adopter guide** ([`docs/guide/`](docs/guide/README.md)): quickstart, the subset by example, how to read and write contracts, how to read each red gate, and what the first three milestones showed. [`docs/agent-prompt.md`](docs/agent-prompt.md) is the block to paste into an agent's instructions. `make guide-test` compiles and lints every example in both.

## Not built yet

The unchecked escape hatch, and adopter pilots (Mo waits on its Phase 2). The full deferred list is in the [project sketch](docs/project-sketch.md#deferred).

## Using Safe C in your project

New here? Start with the [quickstart](docs/guide/quickstart.md).

```
make install PREFIX=$HOME/.local        # libsafec.a, headers, safec.pc, safec-lint
export PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig PATH=$HOME/.local/bin:$PATH
cc main.c $(pkg-config --cflags --libs safec)   # #include <safec/arena.h>
safec-lint                                      # subset v1 over every .c/.h under the current directory
```

`safec-lint` is the rule pack: the Semgrep rules, the clang-tidy config and the compiler warning set that `make lint` uses, installed under `$PREFIX/share/safec`. It needs the tools in [Required tools](#required-tools) but not Frama-C. `SAFEC_LINT_CPPFLAGS="-Iinclude"` adds include paths for your own headers. Application C cannot declare arrays or allocate (see the [subset](docs/subset-v1.md)), so backing storage comes from a small adapter file you keep out of the lint run, as in [`examples/adopter/`](examples/adopter/). To run the same lint and build in your CI, call the reusable workflow [`.github/workflows/safec-adopter.yml`](.github/workflows/safec-adopter.yml) (inputs: `lint-paths`, `lint-cppflags`, `build-command`, `working-directory`, `safec-ref`). `make package-test` checks the install and rule pack from outside the tree.

## Make targets

- `make lint`: Scans application C (`*.c` / `*.h`) outside `stdlib/`, `adapters/`, `tests/`, `fuzz/`, `docs/`, and `research-wiki/` (missing application sources is a pass).
- `make lint-selftest`: Runs the lint fixture catalog under `tests/lint/`.
- `make tsan`: Runs every test that includes `<pthread.h>` under ThreadSanitizer.
- `make model`: Runs TLC on `models/channel.tla` at each queue size in `MODEL_QUEUE_SIZES` (default `1 3`), and on `models/mailbox.tla` at each sender count in `MODEL_SENDERS` (default `2 3`) and each of those queue sizes. Needs Java 11+ and `tla2tools.jar` 1.7.4 (see [`models/README.md`](models/README.md)).
- `make bench`: Builds `tests/bench/*_bench.c` at `-O2` without sanitizers and prints latency. Numbers are recorded, not gated; `make gates` skips it.
- `make test`: Builds every `*_test.c` under `tests/` except `tests/lint/` and `tests/gates/` with `-fsanitize=address,undefined -fno-sanitize-recover=all` and runs each (passes when there are none).
- `make fuzz`: Runs every `fuzz/*_fuzz.c` with libFuzzer for `FUZZ_SECONDS` (default 60). Per-input `-timeout` matches that budget so a hung harness cannot stall for 20 minutes.
- `make gate-selftest`: Checks sanitizer and fuzzer canaries, then checks that the WP canary fails, its exact waiver passes, and an expired waiver fails.
- `make prove`: Runs WP with RTE and unsigned-overflow checks on `stdlib/*.c` except `chan.c` and `mailbox.c`, writing `build/prove.log` and a per-file table of scheduled, proved, waived and unproved goals plus Frama-C's own `N / M` total (`build/prove-table.txt`, `build/prove-summary.json`). Set `PROVE_SRC=tests/gates/wp-canary.c` to exercise the deliberate failure.
- `make waivers`: Prints the complete waiver register and fails on invalid or expired entries.
- `make install` / `make uninstall`: Install or remove the library, headers, `safec.pc` and rule pack under `PREFIX` (default `/usr/local`; `DESTDIR` honored). `make package-test` installs to `build/pkg-prefix`, builds and lints the sample adopter from a scratch directory, and expects the pack to flag `tests/package/subset_break.c`. The manual Adopter workflow (`.github/workflows/adopter.yml`) runs it too; `make gates` does not.
- `make guide-test`: Compiles and lints every code example in `docs/guide/` and `docs/agent-prompt.md`, and runs the quickstart against a scratch install. `make ci` runs it; `make gates` skips it.
- `make check`: Runs `make lint`, then `make test`. The fast default before calling a change done.
- `make gates`: Runs application lint, sanitizer tests, ThreadSanitizer tests, canaries, production fuzzing (5 seconds per harness by default), Frama-C WP with waiver validation, the property clients, then the TLC check. Run it by hand when you want the full gate.

`python3 scripts/sanitize.py fuzz --corpus-dir DIR [fuzz/x_fuzz.c ...]` keeps each harness's corpus in `DIR/<harness>`. `FUZZ_SECONDS` overrides either fuzz budget and must be positive; zero means unlimited to libFuzzer and is rejected. Production harnesses belong in `fuzz/`; deliberately failing canaries belong in `tests/gates/`. Run wrapper regression tests with `python3 tests/gates/runner_test.py` and proof-boundary tests with `python3 tests/gates/proof_test.py`.

## Required tools

`make model` also needs Java 11+ and `tla2tools.jar`. `make -C examples/http` needs libmicrohttpd.

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

The runner reads Frama-C's per-goal JSON report rather than its exit code or the printed N-of-N total. Each unproved goal needs an exact `file`, `function`, and `goal` match in `waivers.toml`, plus `id`, `reason`, `owner`, and an unexpired `expires` date. Empty reports, missing reports, tool errors, and other proof warnings fail the gate. The register holds 12 waivers, all expiring 2027-07-14: the two skipped-guard waivers for each of `arena.c`, `slice.c`, `str.c`, `result.c`, `handle.c` and `tslice.c`. `make waivers` prints them.

Frama-C 33 reports that alignment and function-pointer RTE guards are skipped. The gate records these as `wp-rte-unaligned` and `wp-rte-function-pointer`, with function `<translation-unit>` and the analyzed source file. These gaps require explicit waivers, including when all generated goals pass. See the [Frama-C WP manual](https://frama-c.com/download/frama-c-wp-manual.pdf) for the model and report limitations. The synthetic waivers used by `gate-selftest` exist only in temporary files.

A manual Gates run uploads `build/prove.log`, `build/prove-table.txt`, `build/prove-summary.json`, `build/prove-selftest.log`, `build/gates.log`, and the measured opam installation time in `build/ci-times.txt`. The opam cache makes cold and warm timings distinguishable. The manual Fuzz (long) workflow runs only the fuzz harnesses, one job per `fuzz/*_fuzz.c` at 1800 seconds each by default. The proof canary verifies gate behavior; the library's own proof evidence is `build/prove.log`.
