---
title: "ACSL and CBMC patterns for a bump arena and slices"
created: 2026-10-15
updated: 2026-10-20
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

- **SC-06 string design (17 Oct 2026, unproved locally):** `sc_str` is a typedef of `sc_slice`, so it inherits the epoch check and `slice_storage`. `sc_str_from_lit` copies the literal into the arena, since a literal is not arena storage and `slice_storage` would reject it uncopied. `sc_str_find` is specified with a pure predicate `str_match_at` and states both directions: the result holds exactly when some offset matches, and the returned offset is the least one. `stdlib/str.c` needs the same two translation-unit RTE waivers as the other modules; no WP result is recorded here until CI proves it.

## SC-07: tagged results and options

Decision (SC-07, by the agent, not yet reviewed by Robert): `SC_OPTION(T)` and `SC_RESULT(T, E)` are anonymous structs with a tag and a payload (`some`/`ok`, `value`, `err`), and the shipped `size_t` pair has one accessor per function with an ACSL `requires` on the tag (`r.ok != 0` for `unwrap`, `r.ok == 0` for `unwrap_err`). ACSL cannot live inside a macro without `-pp-annot`, which the prove gate does not pass, so the generic macros give only the type and each instantiation that wants proved accessors writes its contracts by hand. A caller that skips the tag check fails the call-site `requires` goal; the lint rule R13 is the same check for application code that reads `.value` directly. R13 is a Semgrep syntax pattern with no types, so it matches any struct field named `value`.

## SC-09: generation-checked handles

Decision (SC-09, by the agent, not yet reviewed by Robert; CI proves `stdlib/handle.c` 245/245 with only the two standard skipped-guard waivers, and WP rejects all 11 mutants in `tests/proofs/sc09_test.py`): the handle table is one arena allocation of three bytes per slot (state, then a 16-bit generation low byte first), not an array of structs. WP's default typed memory model rejects a cast from arena bytes to a wider type, and the prove gate fails on that warning, so the slots stay `uint8_t` and the contracts read the generation as `lo + 256 * hi`. Freeing increments the generation with an explicit carry, which keeps every proof obligation linear. A slot freed at generation 65535 retires (state 2) instead of wrapping, so no stale handle can match again; the wrap test walks 65,536 generations in milliseconds.

The table is a value (slot pointer, capacity, arena epoch), so an arena reset makes every operation on the old table refuse without writing. The slot bytes are deliberately a raw pointer and not an `sc_slice`, because self-review showed that `sc_slice_copy` over a public slice could roll a generation back and revive a stale handle. A handle carries the epoch and the table's byte offset in the arena besides index and generation: without them, a handle from before a reset, or from a sibling table, matched a new table at the same slot and generation. `sc_handle_get` returns the slot index as an `sc_option_size`, so the caller keeps per-slot data in its own storage and the SC-07 tag check guards the read. Liveness is a runtime check WP proves, not a ghost obligation on the caller, which is what timed out in the ghost-token experiment ([[frama-c]]).

## SC-10: ownership-passing channels

Decision (SC-10, agent design; Robert agreed on 17 Oct 2026 to keep the in-script proof skip with no waiver entry, and to leave the arena-reset check out): `sc_chan` is outside WP with no waiver. Threads and C11 atomics are not in WP's model, so `scripts/prove.py` skips `stdlib/chan.c` (`UNPROVED_MODULES`) and the evidence is unit tests, ThreadSanitizer (`make tsan`) and `models/channel.tla`. `tests/gates/chan_test.py` plants a relaxed publish, a send that keeps the handle, a send into a full queue, a send after close and a LIFO receive, and requires TSan or the tests to go red.

The handle tables are not thread-safe, so each thread owns one and a handle never crosses threads. A send frees the sender's slot (every copy goes stale, which is the model's `NoUseAfterSend`), and a receive issues a fresh handle from the receiver's table. What crosses is the right to the slot plus one `size_t` payload, an index into storage both threads can reach. The queue is a ring of cap + 1 entries (one stays empty), with a release store of `tail` and an acquire load on the other side. `closed` is read before `tail` is read again, so closed-and-empty is final. A receive into a full receiver table returns `SC_CHAN_NOSLOT` and leaves the message queued, a refusal the model does not have. The ring does not carry the arena epoch: the caller must not reset the creating arena while the channel is live. A runtime epoch check would race across threads (the receiver would read an epoch the sender may be resetting), so the rule is a lifetime one, and SC-11 must keep the channel in a long-lived arena, not a per-request one. `tests/gates/chan_test.py` also pins that `chan.c` is the only file `make prove` skips.

## SC-11: writing an HTTP server in the subset

Finding (SC-11, agent, not yet reviewed by Robert): the demo in `examples/http/` is a usage test of the subset, and the parser (`parse.c`) is application code, so WP does not see it; its evidence is `tests/http_parse_test.c` and `fuzz/http_parse_fuzz.c`. Four frictions showed up. (1) Constants are not free: the subset has no static byte tables or `const char *`, and `sc_str_eq` needs both strings in one arena, so `"HTTP/1.1"` and `"GET"` are copied into the request arena on every request (`SC_STR_LIT`) and the parser takes a mutable arena only for that. (2) Scanning is verbose but safe: every byte comes from `sc_slice_get`, so the parser has no index arithmetic to get wrong, and a stale head falls out as a 400. (3) There is nowhere to put a header table, since raw arrays are banned, so the parsed request keeps one validated slice of header lines and `http_header` rescans it. (4) Lint had never compiled an application file: it passed no include path, so the first `#include "safec/..."` failed; it now gets `-Iinclude -Iadapters`. (5) R2 has false positives: Semgrep's untyped match flagged `s.len - pos` in a return expression on an `sc_str` parameter as pointer arithmetic, and the fix was to assign `seen = s.len; seen -= pos;`.

The adapter (`adapters/microhttpd.c`, rule R0) owns everything outside the subset: it rebuilds the request head as bytes in a per-request arena (with libmicrohttpd's percent-decoding switched off, since a decoded `%0d%0a` forged header lines in the first version), copies the reply out with `MHD_RESPMEM_MUST_COPY`, and resets the arena in the request-completed callback. The server runs one polling thread, so a pool of 64 request arenas is single-threaded state. No channel is used; a later one must live in a long-lived arena (see SC-10 above). Numbers are in `docs/evidence/sc11/bench.md`.

## SC-13: literal compare and a narrower R2

Fixes for frictions (1) and (5) above. `sc_str_eq_lit(a, x, lit, n)` (macro `SC_STR_EQ_LIT(a, x, "GET")`) compares a live string with a literal in place: the contract is the same equivalence as `sc_str_eq` with the right-hand side read from `lit` instead of the arena, so a literal no longer needs an arena copy and the parser takes `const sc_arena *`. R2's false positive came from Semgrep resolving `s.len` to a variable named `len` in scope (a `size_t *len` parameter), so the pointer type of that variable leaked onto the field access. R2's typed patterns now require a bare identifier operand. The cost is a known gap: pointer arithmetic on a struct field (`s.ptr + 1`) is not flagged, because Semgrep never typed field accesses reliably in the first place (the old match on `s.ptr` was the same name collision).

## SC-16: the adopter guide

Decision (SC-16, agent, not yet reviewed by Robert): the guide in `docs/guide/` and the prompt in `docs/agent-prompt.md` carry no code the checks do not run. `scripts/guide-check.py` (`make guide-test`, CI step `guide`) requires every `c` block to say what it is: `good` and `avoid` compile under the subset warning set and pass `safec-lint`, `bad <rule>` must raise that rule, `adapter` compiles, and `quote <path>` must be a verbatim, in-order excerpt of a repo file, so the contract examples in the guide are the proved header contracts and not new ACSL that nobody proved (Frama-C was not installable in the authoring session). The quickstart's `sh` blocks run against a scratch install of `make install`. The guide says plainly that application C is not under WP in this repository: the runtime checks do that work, and the contracts bind a caller only if the adopter runs Frama-C over their own code, which nothing here exercises. The write-up of M1 to M3 repeats this page's findings with their limits (no adopter yet, one demo, skipped RTE guards waived, `make mutate` blind to a property the contract never states).

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
