---
title: "ACSL and CBMC patterns for a bump arena and slices"
created: 2026-10-15
updated: 2026-10-16
type: concept
tags: [acsl, formal-verification, model-checking, allocators, bounds-annotations]
sources: [raw/code/acsl-by-example-copy.md, raw/code/acsl-by-example-fill.md, raw/code/acsl-by-example-find.md, raw/code/frama-c-libc-string-h.md, raw/code/frama-c-libc-stdlib-h.md, raw/papers/mangano-2016-contiki-memb.md, raw/code/aws-c-common-byte-buf-c.md, raw/code/aws-c-common-byte-buf-append-harness.md, raw/code/aws-c-common-math-fallback.md, raw/code/s2n-tls-stuffer-c.md, raw/articles/cbmc-memory-predicates.md, raw/articles/cbmc-loop-contracts.md, raw/articles/so-unsigned-wraparound-frama-c.md, raw/papers/frama-c-rte-manual.md, raw/papers/autoacsl-2026.md]
confidence: medium
---

# ACSL and CBMC patterns for a bump arena and slices

These are the patterns milestone 1 (bump arena: init, alloc, reset, plus a length-carrying slice library, proved with [[frama-c]] WP + RTE and with [[cbmc]]) can reuse. Each pattern quotes an existing proved spec. The "Subset rule" lines are **recommendations**, not claims made by the sources.

## 1. Region model: a validity predicate over the struct

Contiki `memb` fixes a whole-store invariant `valid_memb(m)` that is required and ensured by every operation. ^[raw/papers/mangano-2016-contiki-memb.md]

```c
/*@ requires valid_memb(m);
    ensures  valid_memb(m);
    assigns  m->count[0 .. (m->num - 1)]; */
void *memb_alloc(struct memb *m);
```

The CBMC equivalents are executable predicates: `aws_byte_buf_is_valid` (len ≤ capacity, buffer writable for capacity, or all zero/NULL) and s2n's `s2n_stuffer_validate` (read ≤ write ≤ high_water_mark ≤ size). ^[raw/code/aws-c-common-byte-buf-c.md] ^[raw/code/s2n-tls-stuffer-c.md]

```c
return buf != NULL &&
  ((buf->capacity == 0 && buf->len == 0 && buf->buffer == NULL) ||
   (buf->capacity > 0 && buf->len <= buf->capacity &&
    AWS_MEM_IS_WRITABLE(buf->buffer, buf->capacity)));
```

- **Gotchas:** memb needed Coq-proved lemmas for its inductive free-block count, and it lists stronger isolation between blocks as future work. CBMC user predicates must terminate and cannot be mutually recursive. ^[raw/articles/cbmc-memory-predicates.md]
- **Subset rule:** every arena or slice type ships one `valid_*` predicate (an ACSL predicate plus a C `_is_valid` twin for CBMC). Every public function requires and ensures it. Encode the state as monotone cursors (`0 ≤ used ≤ cap`) rather than counts that need inductive definitions.

## 2. Ranges and separation

ACSL by Example `copy` and the Frama-C libc `memcpy` both rely on range validity and `\separated`. ^[raw/code/acsl-by-example-copy.md] ^[raw/code/frama-c-libc-string-h.md]

```c
/*@ requires valid_dest: valid_or_empty(dest, n);
    requires valid_src:  valid_read_or_empty(src, n);
    requires separation:
      \separated(((char *)dest)+(0..n-1),((char *)src)+(0..n-1));
    assigns ((char*)dest)[0..n - 1] \from ((char*)src)[0..n-1]; */
```

- **Gotchas:** libc wraps `n == 0` in `valid_or_empty` (`empty_block(s) || \valid_read(s)`), so a zero-length slice need not point to valid memory. ACSL by Example's `copy` separates `a+(0..n-1)` from `b` (a single pointer) only, which is weaker than libc's range-vs-range form.
- **Subset rule:** slice ops take `(ptr, len)` as one struct, and every copy requires range-vs-range `\separated`. Overlapping moves go through a separate `memmove`-style function. Handle empty slices explicitly with a `len == 0` branch or an `_or_empty` predicate.

## 3. Alloc postconditions: freshness, position, disjointness

The libc `malloc` contract uses `\fresh` plus a null behavior tied to a ghost heap state. ^[raw/code/frama-c-libc-stdlib-h.md]

```c
/*@ allocates \result;
    behavior allocation:
      assumes can_allocate: is_allocable(size);
      ensures allocation: \fresh(\result,size);
    behavior no_allocation:
      assumes cannot_allocate: !is_allocable(size);
      ensures null_result: \result==\null;
    complete behaviors; disjoint behaviors; */
```

`memb_alloc` instead states the position inside the backing array, which gives alignment by construction, plus validity and frame. ^[raw/papers/mangano-2016-contiki-memb.md]

```c
\result == (char*) m->mem + (i * m->size) &&
\forall integer j; (0 <= j < i || i < j < m->num) ==>
   m->count[j] == \old(m->count[j]);
ensures \valid((char*) \result + (0 .. (m->size - 1)));
```

In CBMC, `__CPROVER_is_fresh(p, n)` in an ensures clause checks validity and distinctness from the other fresh pointers in the contract. ^[raw/articles/cbmc-memory-predicates.md]

- **Gotchas:** `\fresh` describes a new allocation block. A bump arena carves from one existing block, so `\fresh` is the wrong model. Use the memb style: `\result == base + old_used` together with `\valid(\result+(0..n-1))`. For disjointness from earlier allocations, ensure `\separated(\result+(0..n-1), base+(0..old_used-1))`. That last clause is our recommendation; memb itself did not prove inter-block isolation.
- **Subset rule:** arena `alloc` returns NULL on any failure, and its contract has two complete, disjoint behaviors (fits / doesn't fit). Require the alignment to be a power of two and state it as `(\result - base) % align == 0`. Ban callers from doing arithmetic on the returned pointer outside slice APIs.
- **SC-05 alignment decision (16 Oct 2026, Robert — "restate + waive"):** `\aligned` is unsupported in Frama-C 33 (WP warns `\aligned not yet implemented` and turns `\aligned` goals into unprovable obligations). Contracts in `include/safec/arena.h` remove `\aligned` and restate 8-byte alignment as integer offset arithmetic that WP proves: `a->used % 8 == 0` in `arena_shape` / `sc_arena_init`, and `(\result.ptr - a->base) % 8 == 0 && a->used % 8 == 0` in `sc_arena_alloc`'s `fits` behavior, with 8-byte alignment of the caller's backing buffer documented as a caller precondition. The four translation-unit RTE guards that Frama-C 33 skips because `\aligned` and `\valid_function` are unsupported (`wp-rte-unaligned` and `wp-rte-function-pointer` on `stdlib/arena.c` and `stdlib/slice.c`) are waived in `waivers.toml`.

## 4. Reset

aws `aws_byte_buf_reset` only sets `len = 0`, optionally zeroing first. ^[raw/code/aws-c-common-byte-buf-c.md]

```c
void aws_byte_buf_reset(struct aws_byte_buf *buf, bool zero_contents) {
    if (zero_contents) { aws_byte_buf_secure_zero(buf); }
    buf->len = 0;
}
```

- **Gotchas:** none of the sources fetched gives an ACSL spec for invalidating earlier sub-pointers after a reset. Memory stays `\valid`, so WP cannot flag use-after-reset unless the spec adds a ghost epoch. This is a gap, not a sourced pattern.
- **SC-05 decision (15 Oct 2026, Robert):** use a runtime `unsigned epoch` in the arena and each slice. The two-arena ghost experiment distinguishes resetting the owning arena from resetting another arena, but accepts a stale slice if its caller recaptures the birth token after reset. Equality between freely supplied ghost tokens alone does not establish a slice's lifetime. This is local experimental evidence, not a result from the sources above.
- `sc_arena_reset` increments the runtime epoch and refuses to wrap at `UINT_MAX`. Slice operations receive their owning arena and reject an epoch mismatch, including after allocation reuses the same address. Reinitializing an arena starts a new lifetime and requires no outstanding slices. Pointer validity and association with the supplied owner remain caller preconditions; the tags do not validate arbitrary C pointers.
- The current library and proof controls are in `stdlib/`, `include/safec/`, and `tests/proofs/`; commands and retained transcripts are in `docs/evidence/sc05/`. With alignment restated as `used % 8 == 0` and `(\result.ptr - a->base) % 8 == 0`, all 203 generated WP goals across `stdlib/arena.c` (84/84) and `stdlib/slice.c` (129/129) prove, and `waivers.toml` waives only the four tool-omitted `<translation-unit>` RTE guards (`wp-rte-unaligned` and `wp-rte-function-pointer`).

## 5. Loops: invariant, assigns, variant

ACSL by Example `copy`, `fill` and `find` use the same three-part loop template. ^[raw/code/acsl-by-example-copy.md] ^[raw/code/acsl-by-example-find.md]

```c
/*@ loop invariant bound:     0 <= i <= n;
    loop invariant equal:     Equal{Pre,Here}(a, i, b);
    loop invariant unchanged: Unchanged{Pre,Here}(a, i, n);
    loop assigns   i, b[0..n-1];
    loop variant n-i; */
for (size_type i = 0u; i < n; ++i) { b[i] = a[i]; }
```

`find` uses `loop assigns i;` and `\forall integer k; 0 <= k < i ==> a[k] != v`, with `some`/`none` behaviors. CBMC loop contracts take `__CPROVER_assigns`, `__CPROVER_loop_invariant` and `__CPROVER_decreases`, in that order, and only with `--apply-loop-contract`. ^[raw/articles/cbmc-loop-contracts.md]

- **Gotchas:** the bound invariant is `i <= n`, not `i < n`. `copy` needs an explicit "source unchanged" invariant to keep the source frame. CBMC ignores loop contracts unless the flag is set; otherwise it unwinds instead.
- **Subset rule:** only index loops of the form `for (size_t i = 0; i < len; ++i)` with no pointer-increment loops, and every loop has an invariant, an assigns clause and a variant. No `break`/early `return` except in find-style loops with a behavior split.

## 6. size_t overflow in sizing math

ACSL arithmetic is over mathematical integers, so `a * b <= UINT_MAX` in a spec is the canonical no-overflow statement. RTE checks unsigned overflow only with `-warn-unsigned-overflow`. ^[raw/articles/so-unsigned-wraparound-frama-c.md] ^[raw/papers/frama-c-rte-manual.md]

aws and s2n check before adding: ^[raw/code/aws-c-common-math-fallback.md] ^[raw/code/s2n-tls-stuffer-c.md]

```c
if ((b > 0) && (a > (UINT64_MAX - b)))
    return aws_raise_error(AWS_ERROR_OVERFLOW_DETECTED);
*r = a + b;
/* s2n */
RESULT_ENSURE_LTE(reserve_obj.write_cursor, UINT32_MAX - reserve_obj.length);
```

The libc `is_allocable` axiom also excludes sizes above `__FC_SIZE_MAX`. ^[raw/code/frama-c-libc-stdlib-h.md]

- **Gotchas:** with default RTE, a wrapping `used + n` in arena code proves "safe". AutoACSL reports that LLM-written specs often fail to enforce integer-overflow constraints. ^[raw/papers/autoacsl-2026.md]
- **Subset rule:** always run WP with `-wp-rte -warn-unsigned-overflow`. Ban raw `+`/`*` on `size_t` sizes and offsets; require `checked_add`/`checked_mul` helpers with ACSL contracts, and write the alloc fit test as `n <= cap - used` (never `used + n <= cap`). Round alignment with a checked helper.

## Not found

- No proved bump or arena allocator turned up in the sources searched (Exa search for a "Frama-C WP proved bump arena"). memb, a fixed-size pool, is the closest.
- `\block_length` and ghost-model arenas do not appear in any fetched spec.
- AutoSpec's paper gives no concrete per-pattern failure notes beyond its general limitations.

## Related

- [[frama-c]]
- [[cbmc]]
- [[specification-synthesis]]
- [[verifier-in-the-loop]]
- [[safe-c-dialects]]
