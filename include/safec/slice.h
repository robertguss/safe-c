#ifndef SAFEC_SLICE_H
#define SAFEC_SLICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Keep the owner and its backing allocation alive while slices exist. */
typedef struct sc_arena {
    uint8_t *base;
    size_t cap;
    size_t used;
    unsigned epoch;
} sc_arena;

typedef struct {
    uint8_t *ptr;
    size_t len;
    unsigned epoch;
} sc_slice;

/*@ predicate arena_shape(sc_arena *a) =
      a->used <= a->cap && a->used % 8 == 0 &&
      (a->cap == 0 || a->base != \null);
    predicate valid_arena(sc_arena *a) =
      \valid_read(a) && arena_shape(a) && a->cap <= PTRDIFF_MAX &&
      (a->cap > 0 ==> \valid(a->base + (0 .. a->cap - 1))) &&
      \separated(a, a->base + (0 .. a->cap - 1));
    predicate slice_storage(sc_arena *a, sc_slice s) =
      s.len == 0 ||
      (\base_addr(s.ptr) == \base_addr(a->base) &&
       \valid(s.ptr + (0 .. s.len - 1)) &&
       s.ptr >= a->base && s.ptr + s.len <= a->base + a->cap);
    predicate valid_slice(sc_arena *a, sc_slice s) =
      slice_storage(a, s) && s.epoch == a->epoch &&
      (s.len == 0 ||
       (s.len <= a->used && s.ptr <= a->base + (a->used - s.len)));
*/

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

/*@ requires valid_arena(a) && slice_storage(a, dst) && slice_storage(a, src);
    requires \separated(dst.ptr + (0 .. dst.len - 1), src.ptr + (0 .. src.len - 1));
    terminates \true;
    exits \false;
    assigns dst.ptr[0 .. dst.len - 1];
    ensures \result <==> valid_slice(a, dst) && valid_slice(a, src) && src.len <= dst.len;
    ensures \result ==> (\forall integer k; 0 <= k < src.len ==> dst.ptr[k] == \old(src.ptr[k]));
    ensures !\result ==> (\forall integer k; 0 <= k < dst.len ==> dst.ptr[k] == \old(dst.ptr[k]));
    ensures \forall integer k; src.len <= k < dst.len ==> dst.ptr[k] == \old(dst.ptr[k]);
*/
bool sc_slice_copy(const sc_arena *a, sc_slice dst, sc_slice src);

/* Move permits overlap. Both slices belong to the same arena. */
/*@ requires valid_arena(a) && slice_storage(a, dst) && slice_storage(a, src);
    terminates \true;
    exits \false;
    assigns dst.ptr[0 .. dst.len - 1];
    ensures \result <==> valid_slice(a, dst) && valid_slice(a, src) && src.len <= dst.len;
    ensures \result ==> (\forall integer k; 0 <= k < src.len ==> dst.ptr[k] == \old(src.ptr[k]));
    ensures !\result ==> (\forall integer k; 0 <= k < dst.len ==> dst.ptr[k] == \old(dst.ptr[k]));
    ensures \forall integer k; src.len <= k < dst.len ==> dst.ptr[k] == \old(dst.ptr[k]);
*/
bool sc_slice_move(const sc_arena *a, sc_slice dst, sc_slice src);

#endif
