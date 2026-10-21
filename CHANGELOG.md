# Changelog

Versions follow `VERSION`. `make install` writes it into `safec.pc` and `include/safec/version.h` (`SAFEC_VERSION`).

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
