# SC-05 evidence

This branch implements the arena and byte-slice modules stacked on SC-04 (PR #7).
Following Robert's 16 October 2026 "restate + waive" decision, alignment in
`include/safec/arena.h` is stated as 8-byte offset arithmetic (`used % 8 == 0`
and `(\result.ptr - a->base) % 8 == 0`) with 8-byte alignment of the caller's
backing buffer as a documented caller precondition, and `waivers.toml` waives
only the four translation-unit RTE guards that Frama-C 33 skips because
`\aligned` and `\valid_function` are unsupported. All 203 generated WP goals
prove (84/84 in `stdlib/arena.c`, 129/129 in `stdlib/slice.c`), and `make gates`
passes.

## Epoch decision

`tests/proofs/ghost-two.c` extends the earlier one-arena experiment with a
2-element ghost epoch array. Resetting arena 1 preserves a token for arena 0;
resetting arena 0 makes the preserved token's call precondition unprovable.
All goals for `stale_recaptured` prove, however: capturing a new token after
reset allows the unchanged C slice through. This experiment checks token
equality, not a binding between a slice and its birth token. It does not rule
out a richer ghost ownership model.

Robert selected runtime epoch tags on 15 October 2026. Each slice stores its
birth epoch and its operations receive the owning arena. Reset fails at
`UINT_MAX` without changing the arena; it cannot wrap and revive old slices.
The unit tests check stale access before and after reusing the same address.
`stale_after_reuse_client` also checks the public contracts from a caller.

This is conditional safety: callers must supply the correct owner and live,
8-byte-aligned backing storage. Initialization starts a new lifetime and must
not replace an arena with outstanding slices. The executable
`sc_arena_is_valid` checks fields; portable C cannot use it to validate
arbitrary pointers.

## Alignment restatement and waivers (16 October 2026)

Frama-C 33 does not implement `\aligned` or `\valid_function`. Rather than carry
unsupported `\aligned` postconditions that time out, `include/safec/arena.h`
expresses 8-byte alignment as integer arithmetic on offsets from `a->base`:
- `arena_shape` and `sc_arena_init` ensure `a->used % 8 == 0`.
- `sc_arena_alloc` (`fits` behavior) ensures
  `(\result.ptr - a->base) % 8 == 0 && a->used % 8 == 0`.
- `waivers.toml` waives the four skipped `<translation-unit>` RTE guards
  (`wp-rte-unaligned` and `wp-rte-function-pointer` on `stdlib/arena.c` and
  `stdlib/slice.c`), owned by `robertguss` and expiring `2027-01-14`.

## Reproduce

Use the pinned tool versions and installation recipe in
`.github/workflows/gates.yml`: Frama-C 33.0, OCaml 5.2.1, Why3 1.8.2,
Alt-Ergo 2.6.4, Z3 4.13.3, clang/clang-tidy 18, and Semgrep 1.181.0.
The local proof runs used Ubuntu 24.04 ARM64 in a disposable Docker container.

```sh
make test
FUZZ_SECONDS=2 make fuzz
make lint-selftest gate-selftest
python3 tests/gates/runner_test.py
python3 tests/gates/proof_test.py
python3 tests/proofs/sc05_test.py
make gates
```

The proof regression suite checks that the baseline modules pass `make prove`
with zero unproved goals and that each seeded mutation causes a specific proof
failure. It checks both requested code mutations (`used += n` failing
`typed_sc_arena_alloc_fits_ensures_aligned_result` and `typed_sc_arena_alloc_ensures`,
and copy's `<=` loop bound failing `sc_slice_copy` loop invariant and RTE memory
access goals), a weakened read-value postcondition (`ensures \true`), a missing
`assigns`, and incomplete allocation behaviors.

The sanitizer unit tests and both fuzz harnesses also pass on macOS with
Homebrew LLVM 23.1.2. Fuzzing compares slice copy/move against libc on a shadow
buffer; it covers both overlap directions and all public functions across the
two modules. This is a short fuzz budget, not an exhaustive search.

Trailing whitespace is removed from retained transcripts; their content is
otherwise unchanged. They come from the source hashes in `source-sha256.txt`.
`prove.log` is the production proof transcript. Mutation logs are separate;
no mutation was left in production code. `gates.log` records the ordered gate.
`proof-tests.log` records the five proof regressions.

## Final local result

- Arena: **84/84** generated goals proved.
- Slices: **129/129** generated goals proved.
- Production gate: **203 proved, 4 waived, 0 unproved** (the four waived goals
  are the per-translation-unit skipped RTE guards `wp-rte-unaligned` and
  `wp-rte-function-pointer` on `stdlib/arena.c` and `stdlib/slice.c`).
- Proof regressions: **5 passed**; gate regressions: **6 passed**; runner
  regressions: **10 passed**. Lint fixtures and sanitizer/WP canaries pass.
- Copy/move contracts frame writes within the destination and preserve bytes
  past the copied prefix. Unit/fuzz cases include larger destinations.
- CI saves the successfully installed toolchain before running gates.
