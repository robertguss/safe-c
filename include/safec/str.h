#ifndef SAFEC_STR_H
#define SAFEC_STR_H

#include "slice.h"

/* A string is a byte slice: length is explicit, there is no terminator and no
 * encoding, and every operation rechecks the owner's epoch like a slice does. */
typedef sc_slice sc_str;

/*@ predicate str_match_at(sc_str hay, sc_str needle, integer i) =
      0 <= i && i + needle.len <= hay.len &&
      (\forall integer k; 0 <= k < needle.len ==> hay.ptr[i + k] == needle.ptr[k]);
*/

/* Copies the n bytes of a literal into the arena; a literal is not arena
 * storage, so slice operations would reject it uncopied. Use SC_STR_LIT for
 * the usual call. Allocation rounds up to 8 bytes; n == 0 and a full arena
 * return the canonical empty string and leave the arena untouched. The caller
 * guarantees lit is readable for n bytes and does not overlap the arena. */
/*@ requires valid_arena(a) && \valid(a);
    requires n == 0 || \valid_read(lit + (0 .. n - 1));
    requires n == 0 || \separated(lit + (0 .. n - 1), a, a->base + (0 .. a->cap - 1));
    terminates \true;
    exits \false;
    assigns a->used, a->base[a->used .. a->cap - 1];
    ensures valid_arena(a);
    ensures \result.epoch == a->epoch;
    behavior fits:
      assumes n > 0 && n <= a->cap - a->used && n <= SIZE_MAX - 7 &&
              (n + 7) / 8 * 8 <= a->cap - a->used;
      ensures \result.ptr == a->base + \old(a->used) && \result.len == n;
      ensures a->used == \old(a->used) + (n + 7) / 8 * 8;
      ensures valid_slice(a, \result);
      ensures \forall integer k; 0 <= k < n ==> \result.ptr[k] == lit[k];
    behavior empty:
      assumes !(n > 0 && n <= a->cap - a->used && n <= SIZE_MAX - 7 &&
                (n + 7) / 8 * 8 <= a->cap - a->used);
      ensures \result.ptr == \null && \result.len == 0;
      ensures a->used == \old(a->used);
    complete behaviors;
    disjoint behaviors;
*/
sc_str sc_str_from_lit(sc_arena *a, const uint8_t *lit, size_t n);

#define SC_STR_LIT(a, lit) sc_str_from_lit((a), (const uint8_t *)(lit), sizeof(lit) - 1)

/* Equal means the same length and the same bytes. A stale string equals
 * nothing, not even itself; two live empty strings are equal. */
/*@ requires valid_arena(a) && slice_storage(a, x) && slice_storage(a, y);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result <==> valid_slice(a, x) && valid_slice(a, y) && x.len == y.len &&
            (\forall integer k; 0 <= k < x.len ==> x.ptr[k] == y.ptr[k]);
*/
bool sc_str_eq(const sc_arena *a, sc_str x, sc_str y);

/* Finds the first occurrence of needle in hay. An empty live needle is found
 * at 0. Stale strings and a needle longer than hay return false and leave
 * *out unchanged. */
/*@ requires valid_arena(a) && slice_storage(a, hay) && slice_storage(a, needle);
    requires \valid(out);
    requires \separated(out, a, a->base + (0 .. a->cap - 1));
    terminates \true;
    exits \false;
    assigns *out;
    ensures \result <==> valid_slice(a, hay) && valid_slice(a, needle) &&
            (\exists integer i; str_match_at(hay, needle, i));
    ensures \result ==> str_match_at(hay, needle, *out) &&
            (\forall integer j; 0 <= j < *out ==> !str_match_at(hay, needle, j));
    ensures !\result ==> *out == \old(*out);
*/
bool sc_str_find(const sc_arena *a, sc_str hay, sc_str needle, size_t *out);

#endif
