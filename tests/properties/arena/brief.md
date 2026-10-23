# Brief: the `arena` module

The task cards this module was built from (text before each card's Result section) and the Before/After of the PR that shipped it.

## SC-05: Add the proven arena and slice type

**Scope:** `sc_slice` (get, sub, copy, move) and `sc_arena` (init, alloc, reset) with ACSL contracts, tests and fuzzers.

**Review gate:** Robert reviews the contracts and the proof log before merge.

## PR #8 summary

Implements the first arena and byte-slice APIs: 8-byte allocation alignment, checked reads/sub-slices, separated copy, overlapping move, and reset lifetime checks. Robert selected runtime epoch tags after the two-arena ghost experiment showed that recapturing a token can hide a stale slice. Reset refuses epoch wraparound.
