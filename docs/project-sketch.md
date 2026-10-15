# Safe C

*Robert Guss, October 2026*

Safe C is planned as a standalone C library. The aim is to make C a practical language for an AI agent to write close to provably safe. Any C project would be able to adopt it. The intended product is a strict subset, a safe standard library, and four gates: lint, sanitizers, fuzz, and Frama-C WP.

That library is not in this repository yet. The only committed prototype is under `docs/evidence/` (this pull request, #3). Later work is listed below with the pull request that delivers it, or as not yet opened.

The planned done rule is: work is done only when `make gates` passes in CI and every proof gap is a listed, unexpired waiver in `waivers.toml`. On this branch `make gates` runs application lint, ASan/UBSan tests, sanitizer/fuzzer canaries, short-budget production fuzzing, and the waiver expiry check. Frama-C WP joins in SC-04; CI wiring is still pending.

## Goal

Strict C has always been possible. People rarely write it, because the rules are tedious and proof annotations are expensive. An agent can repeat bounds checks, ACSL contracts, and fuzz harnesses without tiring of them. C still reaches the toolchains, platforms, and libraries that projects already depend on. Frama-C and TLA+ are mature enough to run in CI. Safe C would put the agent and those checkers on the same change.

## Prototype evidence (this pull request, #3)

`docs/evidence/` holds the throwaway arena/slice sources and the WP and lint logs from that run. Read `docs/evidence/README.md` for the quoted lines.

The WP logs print `Proved goals: 40 / 40` for `arena.c` and `82 / 82` for `arena2.c`. Those counts are goals WP scheduled after skipping RTE guards for unaligned pointers (`\aligned` not supported) and invalid function-pointer calls (`\valid_function` not supported). RTE annotation ran; those guard classes were dropped. That is not full runtime-error discharge. Alignment matters: `arena_alloc` returns `a->base + a->used`.

`ghost.out` prints `Proved goals: 20 / 21`. The unproved goal is `typed_stale_call_slice_get_requires_2` (timeout). That is the intended stale-call rejection after `arena_reset`. Generation-checked handles are not a proved design; they remain a later experiment, informed by this negative prototype.

`wp/mut.c` is a seeded mutation of `arena2.c` (`slice_get` returns `s.ptr[i+1]`). There is no proof log for it.

## Planned stack

The intended stack is linear, from phase 0 through an HTTP demo. SC-01 and SC-02 are merged. SC-03 is open as pull request #6; SC-04 and later work have not started.

### Phase 0 — pull request #4

Merged PR #4 added `AGENTS.md`, `docs/subset-v1.md`, `waivers.toml`, and the lint gate. `make lint` scans application C and fails on a subset break. `make lint-selftest` is the fixture catalog. That PR wired lint and the expired-waiver check into `make gates`; SC-03 adds sanitizers and fuzzing, and SC-04 will add WP. Unsafe C stays in the standard library. A file under `adapters/` may call a C library outside the subset if the rule table lists that file; lint skips `stdlib/` and `adapters/` by path.

### Sanitizer tests and fuzz — pull request #6 (SC-03, open)

Tests build with ASan and UBSan. `make gates` also checks negative canaries and runs every production libFuzzer harness for 5 seconds by default. `make fuzz` defaults to 60 seconds; `FUZZ_SECONDS` overrides either budget. No production tests or harnesses exist yet. Nightly CI remains future work.

### Later pull requests (not opened)

The prover gate would run Frama-C WP on the standard library. A goal that is neither proved nor waived would fail the build. The waiver report would print every waiver and fail when one has expired. CI would run `make gates`. Valgrind and the Clang Static Analyzer would be advisory and would not fail CI.

The prototype did not discharge skipped `\aligned` and `\valid_function` RTE guards. A later prover-gate PR must not treat the 40/40 and 82/82 lines as RTE-complete. Closing those skips, or recording them as waivers, is part of that work.

The first proved application code would be a bump arena and a slice type, in one change. A slice would carry a pointer and a length. Arena allocation would return a slice. Strings would be byte slices, with equality, search, and construction from a literal. Each string function would have a contract that states the result contents. `Result` and `Option` would be tagged structs. Reading the value without checking the tag would fail lint.

Concurrency would start as a TLA+ model of a bounded channel. Send would transfer ownership. TLC would check the model only. Generation-checked handles (a fixed table of slots; a handle is an index plus a generation) are a design to try after the ghost timeout, not a settled result. The channel implementation would follow the model, pass ThreadSanitizer, and stay outside WP, adding no waiver.

The HTTP demo would serve `GET /` through a libmicrohttpd adapter. Each request would get one arena. The demo would reset that arena after the response, and parse headers into strings. The adapter would be the FFI exception, listed in the rule table.

## Deferred

Each item below has no task and no pull request.

- Lean proofs and Lean-to-C generation
- Fil-C as a build mode
- CHERI
- CBMC as a peer prover that can pass the gate
- One event loop per core
- Lock-wrapped shared state
- The FFI story for OpenSSL and libc
- Infer
- h2o
- The agent style guide and prompts
- The unchecked escape hatch
- Adopter pilots
