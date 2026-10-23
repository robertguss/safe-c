# Contracts

Every public function in the standard library has an ACSL contract written
above its declaration in `include/safec/`, and Frama-C WP proves the
implementation against it. This page shows how to read one, how a caller meets
it, and how to write one that WP proves and that a wrong implementation cannot
satisfy. The excerpts are quoted from the headers, and `make guide-test` checks
that each is still a verbatim excerpt (a line holding only `/* ... */` marks
where lines are left out).

The reference module is [`include/safec/slice.h`](../../include/safec/slice.h)
with [`stdlib/slice.c`](../../stdlib/slice.c). When this page and that module
disagree, the module wins.

## Reading a contract

```c quote include/safec/slice.h
/* Stale and out-of-range reads return false and leave *out unchanged.
 * Like all slice operations, this requires storage from the supplied owner;
 * it does not validate arbitrary or dangling C pointers. */
/*@ requires valid_arena(a) && slice_storage(a, s);
    requires \valid(out);
    requires \separated(out, a, a->base + (0 .. a->cap - 1));
    terminates \true;
    exits \false;
    assigns *out;
    ensures \result <==> valid_slice(a, s) && i < s.len;
    ensures \result ==> *out == s.ptr[i];
    ensures !\result ==> *out == \old(*out);
*/
bool sc_slice_get(const sc_arena *a, sc_slice s, size_t i, uint8_t *out);
```

- **`requires`** is what the caller must already know. Here: the arena is
  well formed, the slice's bytes lie inside that arena's storage, and `out` is
  writable and does not overlap the arena. Nothing says the slice is *live*.
  Liveness is checked at run time, and the result says so.
- **`terminates \true; exits \false;`** says the call always returns normally.
- **`assigns *out;`** is the frame: nothing else in memory changes.
- **`ensures \result <==> ...`** pins the return value in both directions. The
  call succeeds exactly when the slice is live and `i` is in range.
- **`ensures !\result ==> *out == \old(*out);`** is the failure contract. A
  failed call leaves the out-parameter as it was.

The comment above the contract is the part a prover cannot check: what the
caller must guarantee about pointers and lifetimes.

## What a caller owes

Application C does not run through WP in this repository: the HTTP demo is a
usage test of the subset, not a proof target. The runtime checks do the work
there, which is why the library fails closed (stale, out-of-range and overflow
inputs all land on one defined result). What a caller owes is in each header
comment. The common ones:

- Keep the arena and its backing buffer alive while slices from it exist.
- Pass the owning arena with every slice. A slice from a different arena, or
  from before `sc_arena_reset`, is refused.
- Give `sc_arena_init` a buffer aligned to 8 bytes.
- Test a result's tag before reading its value. A contract can make that a
  proof obligation, as the accessor below does.

```c quote include/safec/result.h
/* The caller proves o.some first; there is no value to return for none. */
/*@ requires o.some != 0;
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result == o.value;
*/
size_t sc_option_size_unwrap(sc_option_size o);
```

If you run Frama-C over your own code, an `unwrap` call without a proved
`o.some` is an unproved goal. Lint rule R13 catches the same mistake without
Frama-C.

## Writing one that WP proves

A contract is a full specification of the function's results. If a wrong
implementation could satisfy it, it is decoration. Work through these in
order.

**1. Name the caller's obligations with the module predicates.** `requires`
uses `valid_arena` and `slice_storage` instead of spelling out pointer ranges
each time. The predicates are defined once, at the top of the module header.

```c quote include/safec/slice.h
/*@ predicate arena_shape(sc_arena *a) =
      a->used <= a->cap && a->used % 8 == 0 &&
      (a->cap == 0 || a->base != \null);
    predicate valid_arena(sc_arena *a) =
      \valid_read(a) && arena_shape(a) && a->cap <= PTRDIFF_MAX &&
      (a->cap > 0 ==> \valid(a->base + (0 .. a->cap - 1))) &&
      \separated(a, a->base + (0 .. a->cap - 1));
/* ... */
*/
```

**2. Say the function terminates and cannot exit.** Every contract carries
`terminates \true;` and `exits \false;`.

**3. State an exact frame.** `assigns \nothing`, one field, or a range such as
`dst.ptr[0 .. dst.len - 1]`. A frame wider than the code writes lets a bug hide
in the difference.

**4. Define the result in both directions.** Use `<==>`, not `==>`. With `==>`
a function that always returns false satisfies the contract. Then say what
holds on failure.

```c quote include/safec/str.h
/* Equal to a literal without copying it: x has length n and the same bytes as
 * lit. A literal is not arena storage, so sc_str_eq cannot take one; this reads
 * lit in place. A stale x equals nothing. n == 0 matches a live empty x. The
 * caller guarantees lit is readable for n bytes. Use SC_STR_EQ_LIT for the
 * usual call. */
/*@ requires valid_arena(a) && slice_storage(a, x);
    requires n == 0 || \valid_read(lit + (0 .. n - 1));
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result <==> valid_slice(a, x) && x.len == n &&
            (\forall integer k; 0 <= k < n ==> x.ptr[k] == lit[k]);
*/
bool sc_str_eq_lit(const sc_arena *a, sc_str x, const uint8_t *lit, size_t n);
```

**5. Split cases into behaviors, and require them complete and disjoint.**
`complete behaviors` means every input lands in some case, and
`disjoint behaviors` means in only one. Together they leave no input without
a stated result.

```c quote include/safec/slice.h
/* Invalid ranges and stale slices produce the canonical empty slice. */
/*@ requires valid_arena(a) && slice_storage(a, s);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result.epoch == a->epoch;
    behavior fits:
      assumes valid_slice(a, s) && start <= s.len && n <= s.len - start && n > 0;
      ensures \result.ptr == s.ptr + start && \result.len == n;
      ensures valid_slice(a, \result);
    behavior empty:
      assumes !(valid_slice(a, s) && start <= s.len && n <= s.len - start && n > 0);
      ensures \result.ptr == \null && \result.len == 0;
    complete behaviors;
    disjoint behaviors;
*/
sc_slice sc_slice_sub(const sc_arena *a, sc_slice s, size_t start, size_t n);
```

**6. Write sizes in the overflow-free form.** `n <= s.len - start`, never
`start + n <= s.len`. WP runs with unsigned-overflow checks on, and a wrapping
sum would otherwise look safe.

**7. Annotate every loop with an invariant, an exact `loop assigns` and a
variant.** The invariant states what is true after `i` iterations, the
assigns clause names what the loop writes, and the variant shows it ends.

```c quote stdlib/str.c
    /*@ loop invariant 0 <= i <= n;
        loop invariant \forall integer k; 0 <= k < i ==> x.ptr[k] == lit[k];
        loop assigns i;
        loop variant n - i;
    */
    for (size_t i = 0; i < n; i++) {
```

When a proof will not close, strengthen in this order: the loop invariant,
then a helper's contract, then the function's `requires`. A new `requires`
moves the obligation to every caller, so it is the last resort and needs a line
in the header comment.

**8. When Frama-C cannot express a property, restate it.** Frama-C 33 does not
support `\aligned`, so the contracts state 8-byte alignment as offset
arithmetic (`(\result.ptr - a->base) % 8 == 0`) and `waivers.toml` carries the
two guards Frama-C skips for each file. A waiver is a proof gap with an owner
and an expiry date, and an expired one fails `make gates`. The register holds
ten such guard waivers today, all expiring 2027-07-14, and `make waivers`
prints them. Adding another is Robert's decision: propose it, with the
restatement you tried first.

## Checking that the contract is strong enough

A contract that proves is only as strong as what it rejects.

```sh
make prove                      # WP over stdlib/*.c, writes build/prove.log
make prove PROVE_SRC=path/to/mutant.c
make mutate                     # plants code mutations, WP must reject each
```

`make prove PROVE_SRC=...` on a deliberately broken copy must go red. For each
public function, plant one bug (an off-by-one index, a dropped epoch check, a
swapped copy direction) and confirm it does. `make mutate` does this for a
fixed set of mutations in every file `make prove` checks; a mutant that
survives means the contract is weak at that line. It takes about half an hour
on a CI runner, so `make gates` skips it; `make ci` runs it, and `MUTATE_SRC=<file>`
limits it to the file you changed.

`make mutate` finds weakness against the code it covers. It cannot find a
property the contract never states. Safe C's own modules close that gap with a
property manifest (`tests/properties/`): a separate model writes the required
properties from the task brief, without seeing any contract, and `make
properties` proves each one from the contract alone. In your own project, read
your contract once as the caller and ask what a wrong implementation could
still do.

## Where WP does not reach

`stdlib/chan.c`, the bounded channel, and `stdlib/mailbox.c`, the bounded
many-to-one mailbox, are outside WP: threads and atomics are outside its model,
and `make prove` skips both files with no waiver. Their evidence is unit tests,
ThreadSanitizer, a fuzz harness and the TLA+ models in
[`models/`](../../models/README.md), which check the design and not the C.
Application code that uses either must not reset the arena it was created in
while it is in use ([`chan.h`](../../include/safec/chan.h),
[`mailbox.h`](../../include/safec/mailbox.h)), so keep it in a long-lived
arena, not a per-request one. The mailbox suits a scheduler that has many
sending threads and one receiving thread: a send never blocks, a full mailbox
returns `SC_MBOX_FULL` and leaves the sender's handle live, and closing is the
receiver's call, after which every send that was accepted still arrives.
