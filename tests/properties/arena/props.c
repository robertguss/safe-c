#include "safec/arena.h"

/*@
  predicate writable_arena(sc_arena *a) =
    \valid(a) && valid_arena(a);

  predicate slice_candidate(sc_arena *a, sc_slice s) =
    slice_storage(a, s) &&
    (s.len == 0 ||
      (s.len <= a->used &&
       s.ptr <= a->base + (a->used - s.len)));

  predicate slice_input(sc_arena *a, sc_slice s) =
    valid_arena(a) && slice_candidate(a, s);

  predicate slice_pair(sc_arena *a, sc_slice dst, sc_slice src) =
    slice_input(a, dst) && slice_candidate(a, src);

  logic integer rounded_size(integer n) = 8 * ((n + 7) / 8);
*/

/*@
  requires \valid(a);
  requires cap <= PTRDIFF_MAX;
  requires cap > 0 ==> \valid(buf + (0 .. cap - 1));
  requires \separated(a, buf + (0 .. cap - 1));
  terminates \true;
  exits \false;
  assigns *a;
  ensures valid_arena(a);
  ensures a->base == buf && a->cap == cap && a->used == 0;
*/
void prop_init_establishes_empty_arena(sc_arena *a,
                                       uint8_t *buf, size_t cap)
{
    sc_arena_init(a, buf, cap);
}

/*@
  requires valid_arena(a);
  terminates \true;
  exits \false;
  assigns \nothing;
  ensures \result;
*/
bool prop_valid_arena_is_recognized(sc_arena *a)
{
    return sc_arena_is_valid(a);
}

/*@
  requires writable_arena(a);
  requires n > 0;
  terminates \true;
  exits \false;
  assigns a->used;
  ensures valid_arena(a);
  ensures rounded_size(n) <= \old(a->cap - a->used) ==>
    \result.len == n &&
    \result.ptr == a->base + \old(a->used) &&
    \result.epoch == a->epoch &&
    valid_slice(a, \result) &&
    a->used == \old(a->used) + rounded_size(n);
  ensures rounded_size(n) > \old(a->cap - a->used) ==>
    \result.len == 0 && a->used == \old(a->used);
*/
sc_slice prop_alloc_reserves_aligned_space(sc_arena *a, size_t n)
{
    return sc_arena_alloc(a, n);
}

/*@
  requires writable_arena(a);
  requires valid_slice(a, s);
  terminates \true;
  exits \false;
  assigns a->used, a->epoch;
  ensures valid_arena(a);
  ensures \result <==> \old(a->epoch) < UINT_MAX;
  ensures \result ==>
    a->used == 0 && a->epoch == \old(a->epoch) + 1;
  ensures \result ==> !valid_slice(a, s);
  ensures !\result ==>
    a->used == \old(a->used) && a->epoch == \old(a->epoch);
*/
bool prop_reset_checks_epoch_wraparound(sc_arena *a, sc_slice s)
{
    return sc_arena_reset(a);
}

/*@
  requires slice_input(a, s);
  requires \valid(out);
  requires \separated(out, a);
  requires \separated(out, a->base + (0 .. a->cap - 1));
  terminates \true;
  exits \false;
  assigns *out;
  ensures \result <==> (s.epoch == a->epoch && i < s.len);
  ensures \result ==> *out == \old(s.ptr[i]);
  ensures !\result ==> *out == \old(*out);
*/
bool prop_get_checks_lifetime_and_bounds(sc_arena *a, sc_slice s,
                                         size_t i, uint8_t *out)
{
    return sc_slice_get(a, s, i, out);
}

/*@
  requires slice_input(a, s);
  terminates \true;
  exits \false;
  assigns \nothing;
  ensures s.epoch == a->epoch &&
          start <= s.len && n <= s.len - start ==>
    \result.len == n;
  ensures s.epoch == a->epoch &&
          start <= s.len && n <= s.len - start && n > 0 ==>
    \result.ptr == s.ptr + start &&
    \result.epoch == s.epoch && valid_slice(a, \result);
  ensures s.epoch != a->epoch ||
          start > s.len || n > s.len - start ==>
    \result.len == 0;
*/
sc_slice prop_sub_checks_lifetime_and_bounds(sc_arena *a, sc_slice s,
                                             size_t start, size_t n)
{
    return sc_slice_sub(a, s, start, n);
}

/*@
  requires slice_pair(a, dst, src);
  requires \separated(dst.ptr + (0 .. dst.len - 1),
                      src.ptr + (0 .. src.len - 1));
  terminates \true;
  exits \false;
  assigns dst.ptr[0 .. dst.len - 1];
  ensures \result <==>
    (dst.epoch == a->epoch && src.epoch == a->epoch &&
     src.len <= dst.len);
  ensures \result ==> (\forall integer k; 0 <= k < src.len ==>
    dst.ptr[k] == \old(src.ptr[k]));
  ensures \result ==> (\forall integer k; src.len <= k < dst.len ==>
    dst.ptr[k] == \old(dst.ptr[k]));
  ensures !\result ==> (\forall integer k; 0 <= k < dst.len ==>
    dst.ptr[k] == \old(dst.ptr[k]));
*/
bool prop_copy_checks_lifetime_and_capacity(sc_arena *a,
                                            sc_slice dst, sc_slice src)
{
    return sc_slice_copy(a, dst, src);
}

/*@
  requires slice_pair(a, dst, src);
  terminates \true;
  exits \false;
  assigns dst.ptr[0 .. dst.len - 1];
  ensures \result <==>
    (dst.epoch == a->epoch && src.epoch == a->epoch &&
     src.len <= dst.len);
  ensures \result ==> (\forall integer k; 0 <= k < src.len ==>
    dst.ptr[k] == \old(src.ptr[k]));
  ensures \result ==> (\forall integer k; src.len <= k < dst.len ==>
    dst.ptr[k] == \old(dst.ptr[k]));
  ensures !\result ==> (\forall integer k; 0 <= k < dst.len ==>
    dst.ptr[k] == \old(dst.ptr[k]));
*/
bool prop_move_supports_overlap(sc_arena *a, sc_slice dst, sc_slice src)
{
    return sc_slice_move(a, dst, src);
}
