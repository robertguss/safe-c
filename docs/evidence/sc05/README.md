# SC-05 evidence

This is an implementation draft stacked on SC-04 (PR #7), not an accepted
proof of all runtime safety. `waivers.toml` remains empty. The complete gate
fails on Frama-C's omitted alignment and function-pointer model classes for
each of the two library translation units, plus the explicit initialization
and allocation alignment postconditions. Frama-C turns those unsupported targets
to false; it is not a timeout that a longer solver budget can reasonably fix.

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

This is conditional safety: callers must supply the correct owner and live
backing storage. Initialization starts a new lifetime and must not replace
an arena with outstanding slices. The executable `sc_arena_is_valid` checks
fields; portable C cannot use it to validate arbitrary pointers. All buffers
must start at an 8-byte-aligned address. The contracts explicitly require and
ensure alignment, which Frama-C 33 cannot model; the gate retains that gap.

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

The proof regression suite expects the named alignment obligations and model gaps and checks that
each seeded mutation causes an additional, specific failure. A nonzero gate
status alone is insufficient to pass these tests. It checks both requested
code mutations (`used += n` and copy's `<=` loop bound), a weakened read-value
postcondition, a missing `assigns`, and incomplete allocation behaviors.
This is automated regression evidence; Thermos/Robert's review remains pending.

The sanitizer unit tests and both fuzz harnesses also pass on macOS with
Homebrew LLVM 23.1.2. Fuzzing compares slice copy/move against libc on a shadow
buffer; it covers both overlap directions and all public functions across the
two modules. This is a short fuzz budget, not an exhaustive search.

Trailing whitespace is removed from retained transcripts; their content is
otherwise unchanged. They come from the source hashes in `source-sha256.txt`.
`prove.log` is the production proof transcript. Mutation logs are separate;
no mutation was left in production code. `gates.log` records the ordered gate,
including its expected model-gap failure. `proof-tests.log` records the five
proof regressions. Timings in `prove-times.json` measure three full `make prove`
invocations including the expected nonzero exit; they are observations, not a
performance gate.

## Final local result

- Arena: **82/84** generated goals. The named `base_alignment` and
  `aligned_result` targets remain unproved because WP does not implement
  `\aligned`.
- Slices: **129/129** generated goals.
- Production gate: **201 proved, 0 waived, 6 unproved** (the two targets above
  plus four model gaps). WP's printed totals include termination/unreachable
  goals not counted as report entries by the wrapper.
- Proof regressions: **5 passed**; gate regressions: **6 passed**; runner
  regressions: **9 passed**. Lint fixtures and sanitizer/WP canaries pass.
- Three full `make prove` wall times: **7.369 s, 7.289 s, 7.261 s**. These use a
  warm WP cache, including cached unsuccessful results; they do not estimate
  cold proof time or prove completion.

The stronger contract deliberately retains these failures. SC-05 cannot meet
its N/N, zero-waiver acceptance rule with this proof model as it stands.
Do not mark this draft merge-ready based on sanitizer results or partial WP
counts. Resolving the model or changing the zero-waiver policy needs a visible
follow-up decision; neither was done here.

Copy/move contracts frame writes within the destination and preserve bytes
past the copied prefix. Unit/fuzz cases include larger destinations.
CI now saves the successfully installed toolchain before running gates;
proof failures no longer skip that cache save.
