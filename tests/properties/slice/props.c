#include "safec/slice.h"

/*@
  predicate allocated_storage(sc_arena *a, sc_slice s) =
    slice_storage(a, s) &&
    (s.len == 0 ||
      (s.len <= a->used &&
       s.ptr <= a->base + (a->used - s.len)));

  predicate transfer_inputs(sc_arena *a, sc_slice dst, sc_slice src) =
    valid_arena(a) &&
    allocated_storage(a, dst) &&
    allocated_storage(a, src) &&
    dst.len == src.len && src.len > 0;
*/

/*@
  requires valid_arena(a);
  requires allocated_storage(a, s);
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
bool prop_get_checked_read(sc_arena *a, sc_slice s,
                           size_t i, uint8_t *out)
{
    return sc_slice_get(a, s, i, out);
}

/*@
  requires valid_arena(a);
  requires allocated_storage(a, s);
  terminates \true;
  exits \false;
  assigns \nothing;
  ensures
    (s.epoch == a->epoch && start <= s.len && n <= s.len - start)
    ==> \result.len == n;
  ensures
    !(s.epoch == a->epoch && start <= s.len && n <= s.len - start)
    ==> \result.len == 0;
  ensures
    (s.epoch == a->epoch && start <= s.len &&
     n <= s.len - start && n > 0)
    ==> (\result.ptr == s.ptr + start &&
         \result.epoch == a->epoch &&
         valid_slice(a, \result));
*/
sc_slice prop_sub_checked_range(sc_arena *a, sc_slice s,
                                size_t start, size_t n)
{
    return sc_slice_sub(a, s, start, n);
}

/*@
  requires transfer_inputs(a, dst, src);
  requires \separated(dst.ptr + (0 .. dst.len - 1),
                      src.ptr + (0 .. src.len - 1));
  terminates \true;
  exits \false;
  assigns dst.ptr[0 .. dst.len - 1];
  ensures \result <==>
    (dst.epoch == a->epoch && src.epoch == a->epoch);
  ensures \result ==>
    (\forall integer k; 0 <= k < src.len ==>
       dst.ptr[k] == \old(src.ptr[k]));
  ensures !\result ==>
    (\forall integer k; 0 <= k < dst.len ==>
       dst.ptr[k] == \old(dst.ptr[k]));
*/
bool prop_copy_separated_bytes(sc_arena *a, sc_slice dst, sc_slice src)
{
    return sc_slice_copy(a, dst, src);
}

/*@
  requires transfer_inputs(a, dst, src);
  terminates \true;
  exits \false;
  assigns dst.ptr[0 .. dst.len - 1];
  ensures \result <==>
    (dst.epoch == a->epoch && src.epoch == a->epoch);
  ensures \result ==>
    (\forall integer k; 0 <= k < src.len ==>
       dst.ptr[k] == \old(src.ptr[k]));
  ensures !\result ==>
    (\forall integer k; 0 <= k < dst.len ==>
       dst.ptr[k] == \old(dst.ptr[k]));
*/
bool prop_move_overlapping_bytes(sc_arena *a, sc_slice dst, sc_slice src)
{
    return sc_slice_move(a, dst, src);
}
