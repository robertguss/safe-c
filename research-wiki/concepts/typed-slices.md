---
title: "Typed slices for struct and integer arrays"
created: 2026-10-21
updated: 2026-10-21
type: concept
tags: [acsl, formal-verification, allocators, bounds-annotations]
sources: []
confidence: medium
---

# Typed slices for struct and integer arrays

Decision (SC-23, Robert chose option 3 on 2026-10-21): `sc_tslice` is one non-generic type, a byte `sc_slice` plus an element size, with `sc_tslice_get` and `sc_tslice_set` copying one element in or out. Typed `SC_TSLICE_ALLOC/GET/SET(T, ...)` macros pass `sizeof(T)` and turn a wrong-typed pointer into a `-Werror` compile error. Evidence: PR #39, `include/safec/tslice.h`, `tests/lint/good/tslice-arrays.c`. It builds on [[acsl-patterns-arena-slice]] and is proved with [[frama-c]].

Reasons for the shape, each from something that failed or was ruled out:

- A `sc_slice_of(T)` macro generator was dropped because ACSL lives in comments, which the preprocessor strips before a macro expands, so generated functions cannot carry contracts. `SC_OPTION(T)` makes only a type for the same reason.
- One erased type is proved once for every `T`. The stride is `sizeof(T)` and the arena is 8-byte aligned, so every element is aligned for `_Alignof(T) <= 8`; the macros enforce that bound.
- No pointer-returning accessor: it would escape the epoch check. In-place edits are get, modify, set.
- WP rejects `void *` casts inside contracts, so the functions take `uint8_t *` and the macros cast. The `i * elem` bound is nonlinear; it needed a `mul_mono` lemma plus two asserts in `elem_live` before Alt-Ergo and Z3 proved it.
- Fixed state with constant indices (SipHash `v[4]`) is better as a struct of named fields; it needs no stdlib.
- Waivers: the usual two skipped-guard waivers for `tslice.c`. One mutant (`count > SIZE_MAX / elem` to `>=`) is equivalent: that count needs more than `cap <= PTRDIFF_MAX` bytes.

The Mo cases (diag.c fix tables, decimal.c words, `line_starts`) are the SC-18 R1 and R6 hits this removes; the lint fixture shows both shapes passing.

## Related

- [[acsl-patterns-arena-slice]]
- [[frama-c]]
