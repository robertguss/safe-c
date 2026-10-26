# Changelog

Versions follow `VERSION`. `make install` writes it into `safec.pc` and `include/safec/version.h` (`SAFEC_VERSION`).

## Unreleased

Everything merged after v0.2.0.

### Library

- `buf` (`sc_buf`): a byte builder that appends bytes, strings, decimals (`182334`, `182,334`) and durations (`850 ms`, `1.2 s`, `3 min 4 s`) with no `printf`. An append that does not fit writes nothing and sets a sticky overflow flag. ACSL contracts proved by WP, seven blind properties, tests, a differential fuzz harness and the two standard skipped-guard waivers (SC-36).

### Explorer

- Toolchain and scaffold under `examples/explorer/`: `scripts/install-sdl3.sh` builds SDL3 3.4.18 and SDL3_ttf 3.2.2 (pinned by SHA-256) into `build/tools/sdl3`; `.agents/setup` prepares a fresh orb; `ci/Dockerfile` and the Gates toolchain action install the same pins. `make lint` scans the app (SC-32, #63).
- UI adapter (`adapters/ui.c`): R0 for SDL3, SDL3_ttf and Clay (vendored at `adapters/third_party/`). Owns `main`, passes argv as a `sc_tslice` of `sc_str`, and renders `--scene` / `--screenshot` headlessly (SC-33, #64).
- Postico query-view shell in light and dark (`theme.c`, `view_shell.c`), with headless scenes `hello`, `shell-light` and `shell-dark` (SC-34, #65).
- Milestone 2, connect and run one query: thread and memory adapters (SC-35, #69), the message protocol and connection state machines (SC-37, #71), the libpq adapter and worker (SC-38, #72), and connecting, running and showing one result (SC-39, #73).
- `examples/explorer/BREAKAGE.md` logs every Safe C gap; `research-wiki/concepts/safe-c-application-stress-test.md` summarises it.

### Gates and CI

- `make ci` builds the explorer as the `explorer-build` step; `make gates` does not (SC-32, #63).

## 0.2.0 — 2026-10-25

Everything merged after v0.1.0 (e26e397) through f63a4e7.

### Library

- `tslice` (`sc_tslice`): checked arrays of integers or structs in an arena (`SC_TSLICE_GET`/`SET`). ACSL contracts, tests, a fuzz harness and the two standard skipped-guard waivers (SC-23, #39, #42).
- POSIX adapter (`adapters/posix.c`): `open`, `read`, `fstat`, `close` and `sysconf` return `sc_result_size` with the errno in `err`, so application code never reads `errno` (R9). Tests and a fuzz harness, no ACSL (SC-24, #44).
- `sc_handle_alloc` and `sc_handle_free` state "failure changes nothing" at the top level of their contracts, so WP can use it at a call site (SC-27, #51).
- `sc_chan_send` drops a `sc_handle_free` failure branch that cannot fire after a live `sc_handle_get`, matching `mailbox.c` (#59).

### Subset and lint

- R2 flags arithmetic on a pointer field (`s.ptr + n`, `s.ptr - n`, `s->ptr += n`) and on a cast of a field (`(uint8_t *)s.ptr + n`). `scripts/lint.py` reads clang's typed AST for this, since Semgrep cannot type a field; pointer difference and integer fields such as `s.len + n` still pass (SC-31, #53). Its hits are recorded as findings with lines, which fixed a lint crash when #48 and #53 met on main (#54).
- R2 allows pointer difference `p - q` between two bare pointer identifiers: the result is an integer and reads no memory (SC-26, #49).
- R3 no longer flags `unsigned char *` and `signed char *` (byte pointers, like `uint8_t *`). R2 flags arithmetic on a cast of a bare identifier, `(uint8_t *)block + n`, which R3 used to catch only by accident (SC-25, #46, from the Mo trial's casts and byte pointers).
- R6 names the flexible array member (`T data[];`) as flagged and says it belongs in an adapter, with application code holding an `sc_slice` (SC-29, #47).
- `scripts/lint.py --verbose` prints `file:line rule` for every finding after the rule ids; the default output is unchanged (SC-28, #48). The unused `--gates` and `--waivers` flags and `scripts/lint-selftest.sh` are gone (#59).

### Gates and CI

- `make properties` proves each module's property clients in `tests/properties/` from the callee contracts alone. `scripts/blind.py` writes them from the task brief; the gate never reads `waivers.toml` (SC-27, #51). It passes on a tree with no proved module (#56).
- `make check` (lint plus sanitizer tests, about 30 seconds) is the default before calling a change done. GitHub workflows (Gates, Adopter, CI image, Fuzz (long)) run only when started by hand from the Actions tab; nothing runs on push, pull request or a schedule (#52).
- `make genmc`: GenMC 0.19 checks `stdlib/chan.c` and `stdlib/mailbox.c` under the C11 memory model, with 18 planted bugs it must reject (`tests/genmc/`, `tests/gates/genmc_test.py`, #43).
- `make tlaps`: TLAPS proofs of every safety invariant of `models/channel.tla` and `models/mailbox.tla`, for any size (`models/*_proofs.tla`, `tests/models/tlaps_test.py`, #43). The tlapm 1.6.0-pre pin follows upstream's rebuilt tarball (#56).
- `scripts/model.py` gives each TLC run its own work directory, so concurrent runs no longer collide in `/tmp` (#48).
- The mailbox model test runs inside the CI image (#38).
- qgate is removed: its git hooks, `qgate.toml`, `qgate.baseline` and the bundled skill (#51).

### Docs

- Adopter and agent docs, skills and `safec.pc` match the tree: stale R2 gaps, drifting counts and workflow claims are gone (#58).

## 0.1.0 — 2026-10-21

First tagged release, so adopters (Mo first) can pin it.

### Library (`include/safec/`, `stdlib/`)

- `arena`, `slice`, `str`, `result`, `handle`: ACSL contracts proved by Frama-C WP, one libFuzzer harness each.
- `chan`: bounded ownership-passing channel, one sender. Outside WP; tests, ThreadSanitizer, a fuzz harness and `models/channel.tla` are its evidence.
- `mailbox` (`sc_mbox`, #36): many senders, one receiver, for a scheduler. Same evidence as `chan`, plus `models/mailbox.tla`.

### Subset and lint

- Subset v1, rules R0 to R14 (`docs/subset-v1.md`), enforced by `scripts/lint.py` with semgrep rules and clang-tidy.
- R14 rejects `#pragma`, `_Pragma`, `NOLINT` and `nosemgrep` (#29). The text scans moved from `lint.py` into semgrep rules (#34).
- Lint trial over Mo's `src/base`: 164 hits in 13 of 21 files, mostly R1 and R3 (#33, `docs/evidence/sc18/mo-base-lint.md`).

### Gates and CI

- `make gates`: lint, ASan/UBSan tests, ThreadSanitizer tests, canaries, short fuzzing, Frama-C WP, waiver report, TLC models.
- `make mutate` plants 165 mutants and requires WP to reject each; CI shards it into six jobs (#20, #27).
- `make ci` and `make ci-docker` run everything CI runs, locally (#22).
- `make prove` prints a per-file proof table, also in the Gates job summary (#31): 633 goals scheduled, 623 proved, 10 waived, 0 unproved; Frama-C 663/663.
- The nightly fuzz run keeps its corpus and crash inputs (#32).
- Waivers expire on dates; the two standard skipped-guard waivers per file expire 2027-07-14 (#19).
- qgate hooks (`qgate.toml`, `qgate.baseline`) guard the waiver file, mutant excuses, clang flags and proof opt-outs (#28, #35).

### Packaging and docs

- `make install` provides `libsafec.a`, headers, `safec.pc` and `safec-lint`; `make package-test` and `adopter.yml` check them from outside the tree (#24).
- Adopter guide in `docs/guide/` and `docs/agent-prompt.md`, tested by `make guide-test` (#26).
- HTTP demo in `examples/http/` (#17).

### Not in 0.1.0

The unchecked escape hatch for VM hot paths, and adopter pilots (Mo waits on its Phase 2).
