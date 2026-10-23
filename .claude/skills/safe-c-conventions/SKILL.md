---
name: safe-c-conventions
description: Safe C house style for a stdlib module, its ACSL contract, and repo layout. Use when writing or changing C or ACSL in include/, stdlib/, tests/ or fuzz/, adding a module, or judging whether a change fits the architecture.
---

Reference for what Safe C code looks like. The rules a machine checks live in their own sources; read them, this file does not restate them:

- `AGENTS.md`: the done rule (`make check`, plus the on-demand checks its table names), gate commands, toolchain pins, waiver format.
- `docs/subset-v1.md`: the banned constructs for application code (R0 to R14).
- `include/safec/slice.h` and `stdlib/slice.c`: the reference module. When this file and the reference module disagree, the module wins and this file is stale.

## Module shape

A module `x` is four files that land together:

| File | Holds |
| --- | --- |
| `include/safec/x.h` | Public types, shared ACSL predicates, and one contract per public function, written above its declaration. |
| `stdlib/x.c` | The implementation, with loop annotations and contracts on any `static` helper. Only `stdlib/` may use unsafe C (subscripts, pointer arithmetic). |
| `tests/x_test.c` | Sanitizer unit tests. |
| `fuzz/x_fuzz.c` | A libFuzzer harness. |

A module that needs another includes its header (`arena.h` includes `slice.h`). Code that calls a C library outside the subset is an adapter under `adapters/` and gets a row under R0 in `docs/subset-v1.md`.

## API shape

- Prefix every public name with `sc_`: `sc_slice_get`, `sc_arena`.
- Pass small value types by value (`sc_slice s`) and owners by pointer (`const sc_arena *a`). The owner comes first.
- Failure is a value. A function that can fail returns `bool` and writes its result through an out-parameter it leaves untouched on failure, or returns a canonical empty value (`{NULL, 0, epoch}`). Each failure mode is total: stale, out-of-range, and overflow inputs all land on the same defined result.
- Validity is checked at runtime, never assumed. A slice carries the owner's epoch, and every operation rechecks it (`slice_live`). Arithmetic is written in the overflow-free form: `n <= s.len - start`, never `start + n <= s.len`.
- The header comment above a declaration states the caller's obligations in one or two lines: what the function cannot check (pointer provenance, buffer lifetime, alignment of caller storage).

## Contract shape

A contract is a full specification of the function's results, strong enough that a wrong implementation fails the proof. Each public contract carries:

- `requires` for every caller obligation, named with the module predicates (`valid_arena`, `slice_storage`) rather than spelled inline.
- `terminates \true;` and `exits \false;`.
- An exact `assigns` frame: `\nothing`, a field, or a slice range such as `dst.ptr[0 .. dst.len - 1]`.
- `ensures \result <==> ...` in both directions, plus what holds on failure (`!\result ==> *out == \old(*out)`).
- `behavior` blocks with `complete behaviors; disjoint behaviors;` when results split into cases.

Loops carry `loop invariant`, an exact `loop assigns`, and a `loop variant`.

When Frama-C cannot express a property, restate it in what WP can prove and waive only the guard Frama-C skips. Alignment is the standing example: Frama-C 33 lacks `\aligned`, so contracts state 8-byte alignment as offset arithmetic (`(\result.ptr - a->base) % 8 == 0`) and `waivers.toml` carries the `wp-rte-unaligned` and `wp-rte-function-pointer` guards per file. A new waiver is Robert's decision; propose it, with the restatement you tried.

## Docs that move with the code

A change that adds a module or a gate also updates the sentences in `AGENTS.md` that list what the tree holds ("The arena and slice modules are in this tree."). A design decision with a reason behind it goes into the research wiki concept page it belongs to (see the `research-wiki` skill), so the next agent reads the reason and not just the code.
