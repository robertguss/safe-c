# Brief: the `tslice` module

The task cards this module was built from (text before each card's Result section) and the Before/After of the PR that shipped it.

## SC-23: Typed slices for struct and integer arrays

**Scope:** Add a slice over elements that are not bytes, so code like Mo's can index arrays of structs and integers without raw subscripts. The SC-18 trial found R1 (subscripts) 76 times and R6 (raw arrays) 10 times in Mo's `src/base`, about half of all 164 lint hits, and `sc_slice` holds bytes only. Design first: a generator macro in the style of `SC_OPTION` and `SC_RESULT` (a `sc_slice_of(T)` with checked get and set over an arena allocation), or a fixed set of element types. Robert picks the design before anything past a prototype. Cover constant-index fixed state (SipHash's four words, the 256-bit multiply) in the same design or say why it stays out.

**Files:** a new header and `stdlib/` module, ACSL contracts, `tests/`, a `fuzz/` harness, lint fixtures under `tests/lint/`, a section in `research-wiki/concepts/acsl-patterns-arena-slice.md` and a `log.md` entry. Evidence: `docs/evidence/sc18/mo-base-lint.md`.

**Done when:**

- [x] Robert has chosen the design from the options posted.
- [x] The module has ACSL contracts that `make prove` checks, a fuzz harness, and tests.
- [x] A Mo-shaped example (an array of structs and a fixed integer state) compiles and passes `safec-lint` with no R1 or R6 hit.
- [x] The wiki section and log entry are in.
- [x] `make gates` is green in CI.

**Needs:** Frama-C in CI for the proofs; the build thread cannot install it.

### Design decision (21 Oct 2026)

Robert chose **Option 3** of four in the design thread: one non-generic `sc_tslice` (pointer, length in elements, element size, epoch), proved once in the byte-copy style of `sc_slice_copy`, with typed macros such as `SC_TSLICE_GET(T, …)` as a facade that holds no logic. The card's original idea, a `sc_slice_of(T)` generator macro, was dropped because ACSL can't live inside a macro, so the generated functions would have no contracts.

- Functions take the arena first, like `sc_slice`: `sc_arena_alloc_t`, `sc_tslice_get`, `sc_tslice_set`, `sc_tslice_sub`, `sc_tslice_copy`. A stale epoch, a wrong element size, an index out of range, an overflowing `count * elem` or a misaligned element returns false and leaves `*out` untouched.
- Mo mapping: `diag.c`'s struct tables, `decimal.c`'s words and `line_starts` become `sc_tslice`s. SipHash's `v[4]` in `hash.c` becomes a struct of named fields, with no stdlib change. R3, R2 and R9 hits stay with the other SC-18 follow-ups.
- Open questions for the build: whether to add `sc_tslice_at` returning a checked `void *` for in-place edits (it would skip the epoch check), whether to ship an `SC_TSLICE(T)` typedef macro, and the subset-guide and `AGENTS.md` updates. The new file's two standard waivers are Robert's call.

## PR #39 Before and After

Before: `sc_slice` holds bytes only, so Mo's struct tables (`diag.c`) and integer arrays (`decimal.c`, `line_starts`) have no safe form (SC-18: 76 R1 and 10 R6 hits in `src/base`).

After: `sc_tslice` (design option 3, chosen by Robert): a byte `sc_slice` plus an element size. `sc_tslice_alloc`, `_len`, `_get`, `_set` copy elements in and out, with every `sc_slice` check (epoch, bounds) plus an element-size match. `SC_TSLICE_ALLOC/GET/SET(T, ...)` macros pass `sizeof(T)`, reject `_Alignof(T) > 8`, and turn a wrong-typed pointer into a `-Werror` compile error. One set of ACSL contracts covers every `T`. Open questions settled: no pointer-returning accessor (it would escape the epoch check), and no `SC_TSLICE(T)` type macro (the type is not generic).
