---
name: safe-c-testing
description: Evidence a Safe C function needs before it counts as done (unit tests, fuzz harness, proof, mutation check) and how to read a red gate. Use when writing tests or harnesses, strengthening a contract, or diagnosing a failure from make gates or CI.
---

Every public function carries three layers of evidence, and each one goes **red** on a planted bug. Evidence that stays green on a wrong implementation is decoration.

Commands, budgets, and toolchains are in `AGENTS.md`. CI runs more than `make gates`: the "Run gates and fixture suites" step in `.github/workflows/gates.yml` is the full list, and green on that list is the bar.

## Unit tests: `tests/x_test.c`

Plain `assert` in `main`, built with ASan and UBSan. Cover every behavior the contract names, at its **edges**:

- empty input, length 0, and the zero-sized request;
- the last valid index and the first invalid one (`len - 1`, `len`);
- `SIZE_MAX` in every size and index argument;
- stale input: after `sc_arena_reset` on the owner, and still valid after a reset of a *different* arena;
- the failure contract: preload the out-parameter or destination with a sentinel, and assert it is unchanged after the call fails;
- overlap, for anything that reads and writes the same storage.

Done when each `ensures`, `behavior`, and failure clause in the header has an assertion that would fail if that clause were false.

## Fuzz harness: `fuzz/x_fuzz.c`

The harness is **differential**: it runs the Safe C call and a trusted reference on the same input and compares results byte for byte. `fuzz/slice_fuzz.c` checks `sc_slice_copy` and `sc_slice_move` against `memcpy` and `memmove` on a mirror buffer. A harness that only calls the API catches crashes; the reference model catches wrong answers.

Derive every size and offset from the input bytes with `%`, so the fuzzer reaches the boundaries. End each run with the stale path: reset the owner and assert that every old handle is rejected.

## Proof: `make prove`

`build/prove.log` lists each goal. A line beginning `unproved` is a failure; `waived` matched an entry in `waivers.toml`. The goal name says which clause failed: it carries the function and the clause kind (`ensures`, `loop_invariant`, `assigns`, `requires` at a call site). Strengthen in this order: the loop invariant, then the helper's contract, then the function's `requires`. Adding a `requires` moves the obligation onto every caller, so it is the last resort and needs a line in the header comment.

## Mutation check

A contract that proves is only as strong as what it rejects. For each public function, plant a **mutant** (an off-by-one index, a dropped epoch check, a swapped copy direction) and confirm `make prove PROVE_SRC=<mutant file>` fails on it. `tests/proofs/sc05_test.py` is the model: it copies the tree into a temp dir, applies each mutation, and asserts WP goes red. New modules add their mutants to a test like it so CI keeps the contract honest.

Done when every public function has at least one mutant that WP rejects.

## Reading a red gate

Gates run in order: lint, sanitizer tests, canaries, fuzz, prove, waivers. Fix the first red stage; later stages often fail as a consequence.

- **Lint**: the rule id (`R2-no-ptr-arith`) maps to a row in `docs/subset-v1.md`.
- **Sanitizer or fuzz**: rerun the single test or the saved crash input to get a stack trace. A fuzz crash input becomes a unit test case before the fix.
- **Prove**: as above. A timeout is a real failure; simplify the invariant or split the function before raising any timeout.
- **Waivers**: an expired waiver needs Robert to extend it or the proof gap closed.
- **CI only**: download the `gates-*` artifact (`build/prove.log`, `build/gates.log`) and read the log there. A pinned tool version differing from your local one is the usual cause.
