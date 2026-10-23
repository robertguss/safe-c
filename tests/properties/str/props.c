#include "safec/str.h"

/*@
  predicate str_input(sc_arena *a, sc_str s) =
    valid_arena(a) && slice_storage(a, s);

  predicate str_pair_input(sc_arena *a, sc_str x, sc_str y) =
    str_input(a, x) && slice_storage(a, y);

  predicate literal_input(uint8_t *lit, integer n) =
    0 <= n <= PTRDIFF_MAX &&
    (n > 0 ==> \valid_read(lit + (0 .. n - 1)));

  predicate same_bytes(sc_str x, sc_str y) =
    x.len == y.len &&
    (\forall integer k; 0 <= k < x.len ==> x.ptr[k] == y.ptr[k]);

  predicate literal_bytes(sc_str x, uint8_t *lit, integer n) =
    x.len == n &&
    (\forall integer k; 0 <= k < n ==> x.ptr[k] == lit[k]);

  predicate construction_input(sc_arena *a,
                               uint8_t *lit, integer n) =
    valid_arena(a) && \valid(a) && literal_input(lit, n) &&
    ((n + 7) / 8) * 8 <= a->cap - a->used &&
    \separated(a, lit + (0 .. n - 1)) &&
    \separated(a->base + (0 .. a->cap - 1),
               lit + (0 .. n - 1));

  predicate search_input(sc_arena *a, sc_str hay,
                         sc_str needle, size_t *out) =
    str_pair_input(a, hay, needle) && \valid(out) &&
    \separated(out, a, a->base + (0 .. a->cap - 1));
*/

/*@
  requires construction_input(a, lit, n);
  terminates \true;
  exits \false;
  assigns a->used, a->base[0 .. a->cap - 1];
  ensures valid_arena(a);
  ensures valid_slice(a, \result);
  ensures \result.len == n;
  ensures \forall integer k; 0 <= k < n ==>
    \result.ptr[k] == \old(lit[k]);
*/
sc_str prop_from_lit_content(sc_arena *a, const uint8_t *lit, size_t n)
{
    return sc_str_from_lit(a, lit, n);
}

/*@
  requires str_pair_input(a, x, y);
  terminates \true;
  exits \false;
  assigns \nothing;
  ensures \result <==>
    (valid_slice(a, x) && valid_slice(a, y) && same_bytes(x, y));
*/
bool prop_eq_exact(const sc_arena *a, sc_str x, sc_str y)
{
    return sc_str_eq(a, x, y);
}

/*@
  requires str_input(a, x);
  requires literal_input(lit, n);
  terminates \true;
  exits \false;
  assigns \nothing;
  ensures \result <==>
    (valid_slice(a, x) && literal_bytes(x, lit, n));
*/
bool prop_eq_lit_exact(const sc_arena *a, sc_str x,
                       const uint8_t *lit, size_t n)
{
    return sc_str_eq_lit(a, x, lit, n);
}

/*@
  requires search_input(a, hay, needle, out);
  terminates \true;
  exits \false;
  assigns *out;
  ensures \result <==>
    (valid_slice(a, hay) && valid_slice(a, needle) &&
     (\exists integer i; str_match_at(hay, needle, i)));
  ensures \result ==> str_match_at(hay, needle, *out);
  ensures \result ==>
    (\forall integer i; 0 <= i < *out ==>
      !str_match_at(hay, needle, i));
*/
bool prop_find_first_match(const sc_arena *a, sc_str hay,
                          sc_str needle, size_t *out)
{
    return sc_str_find(a, hay, needle, out);
}
