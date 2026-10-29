# Safe C

*Robert Guss, October 2026*

Safe C is a standalone C library. The aim is to make C a practical language for an AI agent to write close to provably safe. Any C project can adopt it. The product is a strict subset, a safe standard library, and gates that check both: lint, sanitizers, fuzzing, Frama-C WP, and a TLA+ model of the channel.

Every proof gap is a listed, unexpired waiver in `waivers.toml`. Checks run by hand: `make check` before calling a change done, and the heavier gates when a change calls for them (see `AGENTS.md`). This document describes the tree as it stands on `main`; the Notion hub's Tasks database tracks what comes next.

## Goal

Strict C has always been possible. People rarely write it, because the rules are tedious and proof annotations are expensive. An agent can repeat bounds checks, ACSL contracts, and fuzz harnesses without tiring of them. C still reaches the toolchains, platforms, and libraries that projects already depend on. Frama-C and TLA+ are mature enough to run in CI. Safe C puts the agent and those checkers on the same change.

## What is built

### Subset and lint

`docs/subset-v1.md` lists rules R0 to R14. `make lint` scans application C and fails on a subset break; `make lint-selftest` runs the fixture catalog under `tests/lint/`. Unsafe C lives only in `stdlib/`. A file under `adapters/` may call a C library outside the subset if the rule table lists it (rule R0); lint skips `stdlib/` and `adapters/` by path. Rule R13 flags reading a `Result` or `Option` value without checking its tag, and R14 rejects pragmas and `NOLINT` comments that would switch a check off.

### Standard library

Each module has a header in `include/safec/` and a source in `stdlib/`:

- **arena**: a bump allocator over a caller buffer. A runtime epoch tag changes on reset, so slices taken before a reset are rejected. This replaced the ghost-epoch idea, which was not sound across two arenas.
- **slice**: a pointer and a length, with checked get, sub-slice, copy and move. Allocation returns a slice.
- **tslice**: a checked array of equal-sized elements (integers or structs) in an arena (`SC_TSLICE_GET`/`SET`).
- **str**: strings as byte slices, with construction from a literal, equality and search.
- **result**: `Result` and `Option` as tagged structs (macros `SC_RESULT` and `SC_OPTION`, instantiated for `size_t`).
- **handle**: a fixed table of slots; a handle is an index plus a generation. Resolving a stale handle fails at runtime and WP proves the check.
- **buf**: a byte builder that appends bytes, strings, decimals and durations into a destination slice, with a sticky overflow flag.
- **utf8**: RFC 3629 decode, validate, count, step back one character and cut at a character boundary.
- **chan**: a bounded single-producer, single-consumer channel that passes ownership of each message through handles. Sending invalidates the sender's handle.
- **mailbox**: a bounded many-to-one mailbox (`sc_mbox`) that passes ownership of each message through handles. Closing is a receiver action.

Every public function except the channel's and mailbox's has an ACSL contract and a fuzz harness. `stdlib/chan.c` and `stdlib/mailbox.c` are the exceptions: threads and atomics are outside WP, so `make prove` skips them with no waiver. Their evidence is the tests, ThreadSanitizer (`make tsan`), fuzz harnesses, and the TLA+ models. Reusing a channel's or mailbox's arena while it is live is a documented caller obligation, not a runtime check.

### Gates

`make gates` runs, in order: application lint; ASan/UBSan tests; ThreadSanitizer tests; sanitizer and fuzzer canaries; short-budget fuzzing (5 seconds per harness by default); Frama-C WP with waiver validation; the property clients; and TLC on `models/channel.tla` at queue sizes 1 and 3 and on `models/mailbox.tla` at sender counts 2 and 3 and each of those queue sizes. `make fuzz` defaults to 60 seconds per harness and `FUZZ_SECONDS` overrides either budget. `make bench` records latency and is not gated.

The proof gate reads Frama-C's per-goal report. A goal must be valid or matched exactly by a waiver's file, function and goal. Frama-C 33 skips two RTE guard classes (`\aligned` and `\valid_function` are not supported); the gate records them as the goals `wp-rte-unaligned` and `wp-rte-function-pointer`, so an N-of-N total alone is not full RTE coverage. SC-05 restated alignment as a provable offset fact and waived the skipped guards.

### Waivers

`waivers.toml` holds 18 waivers: the two skipped-guard goals for each of `arena.c`, `slice.c`, `str.c`, `result.c`, `handle.c`, `tslice.c`, `buf.c`, `utf8.c` and `examples/explorer/sqltok.c`. All expire on 2027-07-14. An expired waiver fails `make gates`, and `make waivers` prints the register.

### TLA+ model

`models/channel.tla` models the bounded channel with ownership transfer. TLC checks no double owner, no use after send, nothing after close, FIFO order, the queue bound, and draining. `models/README.md` maps each action to its C function, and `tests/models/channel_test.py` plants a bug per property and requires TLC to find it. TLC checks the design, not the C.

### CI

The GitHub Actions workflows run only when started by hand. Gates runs `scripts/ci.py --skip mutate` plus the mutate shards, with pinned toolchains (Frama-C 33.0, Alt-Ergo 2.6.4, Why3 1.8.2, Z3 4.13.3), uploads the proof and model logs, and caches opam. Fuzz (long) runs each harness for 1800 seconds by default. Valgrind and the Clang Static Analyzer are advisory. `make ci` runs the same checks locally.

### HTTP demo

`examples/http/` serves `GET /` through a libmicrohttpd adapter (`adapters/microhttpd.c`, the rule R0 exception). Each request gets one 16 KiB arena, reset when the request completes, and the parser reads the request head into strings. Application C stays inside the subset and `make lint` scans it. The parser has a unit test and a fuzz harness; it is not under `make prove`, so the demo is a usage test of the subset, not a proof target. `make gates` does not build the server; CI does. It served 100,000 requests with no memory growth (`docs/evidence/sc11/bench.md`).

### Postgres explorer

`examples/explorer/` is a Postico-style Postgres client that stress-tests Safe C on a real program. Application C stays inside the subset; gaps and fixes are logged in `examples/explorer/BREAKAGE.md` and summarised in the research wiki. Milestones 1–6 are implemented on `main`; native Mac acceptance for milestone 6 remains open.

### Evidence and research

`docs/evidence/` keeps the prototype WP and lint logs from the first run and the proof, mutation and benchmark logs from later tasks. The research wiki in `research-wiki/` records the sources and design decisions behind the rules. Workflow guides for agents live in `.claude/skills/`. `docs/guide/` is the adopter guide (quickstart, the subset by example, contracts, red gates and a write-up of what the first three milestones showed) and `docs/agent-prompt.md` is the prompt for an agent that writes Safe C; `make guide-test` compiles and lints their examples.

## Prototype findings

The October 2026 prototype (`docs/evidence/wp/`) showed that Frama-C WP proves a small arena and slice library in seconds, that a seeded code bug breaks proofs, and that a ghost field inside a struct is rejected by Frama-C 33. A file-scope ghost epoch worked for one arena but accepted a recaptured token after reset when two arenas were tested, so SC-05 chose runtime epoch tags. Those logs predate the library; the current proof evidence is `build/prove.log` from `make prove`.

## Deferred

None of these has a task or a pull request, and none is a close criterion.

- Lean proofs and Lean-to-C generation
- Fil-C as a build mode
- CHERI
- CBMC as a peer prover that can pass the gate
- One event loop per core
- Lock-wrapped shared state
- The FFI story for OpenSSL and libc
- Infer
- h2o
- The unchecked escape hatch, which waits until profiling shows a need
- Adopter pilots. The Mo pilot waits on Mo's Phase 2 (target 20 November); its first step would be building Mo's Phase 3 scheduler and channels with Safe C.
