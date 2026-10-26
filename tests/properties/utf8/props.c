#include "safec/utf8.h"

/*@
  predicate utf8_client_input(sc_arena *a, sc_str s) =
    valid_arena(a) &&
    slice_storage(a, s) &&
    (s.epoch == a->epoch ==> valid_slice(a, s));

  predicate utf8_client_window_boundary(uint8_t *p,
                                        integer lo, integer hi) =
    \exists integer k;
      0 <= k && lo <= k <= hi && utf8_boundary(p, k);

  predicate utf8_client_cut(uint8_t *p, integer lo,
                            integer hi, integer chosen) =
    0 <= chosen && lo <= chosen <= hi &&
    (utf8_client_window_boundary(p, lo, hi) ==>
      utf8_boundary(p, chosen)) &&
    (\forall integer k;
      0 <= k && lo <= k <= hi && utf8_boundary(p, k) ==>
      k <= chosen) &&
    (!utf8_client_window_boundary(p, lo, hi) ==> chosen == hi);
*/

/*@
  requires utf8_client_input(a, s);
  terminates \true;
  exits \false;
  assigns \nothing;
  ensures !valid_slice(a, s) || i >= s.len ==>
    \result.len == 0 && \result.cp == 0 && !\result.valid;
  ensures valid_slice(a, s) && i < s.len &&
          utf8_seq_len(s.ptr + i, s.len - i) == 0 ==>
    \result.len == 1 && \result.cp == 0 && !\result.valid;
  ensures valid_slice(a, s) && i < s.len &&
          utf8_seq_len(s.ptr + i, s.len - i) > 0 ==>
    \result.valid &&
    \result.len == utf8_seq_len(s.ptr + i, s.len - i) &&
    \result.cp == utf8_seq_cp(s.ptr + i,
                              utf8_seq_len(s.ptr + i, s.len - i));
*/
sc_utf8_char prop_next_decodes_or_reports_failure(
    const sc_arena *a, sc_str s, size_t i)
{
    return sc_utf8_next(a, s, i);
}

/*@
  requires utf8_client_input(a, s);
  terminates \true;
  exits \false;
  assigns \nothing;
  ensures \result <==>
    (valid_slice(a, s) && utf8_bad(s.ptr, s.len) == 0);
*/
bool prop_valid_characterizes_complete_walk(const sc_arena *a, sc_str s)
{
    return sc_utf8_valid(a, s);
}

/*@
  requires utf8_client_input(a, s);
  terminates \true;
  exits \false;
  assigns \nothing;
  ensures valid_slice(a, s) ==>
    \result == utf8_count(s.ptr, s.len);
  ensures !valid_slice(a, s) ==> \result == 0;
*/
size_t prop_count_characterizes_complete_walk(const sc_arena *a, sc_str s)
{
    return sc_utf8_count(a, s);
}

/*@
  requires utf8_client_input(a, s);
  terminates \true;
  exits \false;
  assigns \nothing;
  ensures !valid_slice(a, s) || i == 0 || i > s.len ==>
    \result == 0;
  ensures valid_slice(a, s) && 0 < i <= s.len ==>
    utf8_client_cut(s.ptr, (integer)i - 4,
                    (integer)i - 1, \result);
*/
size_t prop_prev_selects_nearest_boundary(
    const sc_arena *a, sc_str s, size_t i)
{
    return sc_utf8_prev(a, s, i);
}

/*@
  requires utf8_client_input(a, s);
  terminates \true;
  exits \false;
  assigns \nothing;
  ensures !valid_slice(a, s) ==>
    \result.ptr == \null && \result.len == 0;
  ensures valid_slice(a, s) ==>
    \result.ptr == s.ptr && \result.epoch == s.epoch &&
    \result.len <= s.len && valid_slice(a, \result);
  ensures valid_slice(a, s) && max >= s.len ==>
    \result.len == s.len;
  ensures valid_slice(a, s) && max < s.len ==>
    utf8_client_cut(s.ptr, (integer)max - 3, max, \result.len);
  ensures valid_slice(a, s) && utf8_bad(s.ptr, s.len) == 0 ==>
    utf8_bad(\result.ptr, \result.len) == 0;
*/
sc_str prop_truncate_selects_valid_prefix(
    const sc_arena *a, sc_str s, size_t max)
{
    return sc_utf8_truncate(a, s, max);
}
