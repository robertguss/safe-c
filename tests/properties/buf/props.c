#include "safec/buf.h"

/*@
  predicate client_buf(sc_arena *a, sc_buf *b) =
    valid_arena(a) &&
    \valid(b) &&
    buf_shape(a, *b) &&
    \separated(b, a, a->base + (0 .. a->cap - 1));

  predicate append_effect{Before,After}
      (sc_buf *b, integer count, boolean ok) =
    \at(b->dst.ptr, After) == \at(b->dst.ptr, Before) &&
    \at(b->dst.len, After) == \at(b->dst.len, Before) &&
    \at(b->dst.epoch, After) == \at(b->dst.epoch, Before) &&
    \at(b->len, After) ==
      \at(b->len, Before) + (ok ? count : 0) &&
    (ok ? !\at(b->overflow, After) : \at(b->overflow, After)) &&
    (\forall integer i;
      0 <= i < \at(b->dst.len, Before) &&
      (!ok || i < \at(b->len, Before) ||
       \at(b->len, Before) + count <= i) ==>
        \at(b->dst.ptr[i], After) == \at(b->dst.ptr[i], Before));
*/

/*@ terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result.dst.ptr == dst.ptr;
    ensures \result.dst.len == dst.len;
    ensures \result.dst.epoch == dst.epoch;
    ensures \result.len == 0;
    ensures !\result.overflow;
*/
sc_buf prop_new_empty(sc_slice dst) {
    return sc_buf_new(dst);
}

/*@ requires valid_arena(a);
    requires buf_shape(a, b);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures valid_slice(a, b.dst) && b.len > 0 ==>
      \result.ptr == b.dst.ptr &&
      \result.len == b.len &&
      \result.epoch == b.dst.epoch;
    ensures !valid_slice(a, b.dst) || b.len == 0 ==>
      \result.ptr == \null &&
      \result.len == 0;
*/
sc_str prop_view_written_bytes(const sc_arena *a, sc_buf b) {
    return sc_buf_view(a, b);
}

/*@ requires client_buf(a, b);
    requires n == 0 || \valid_read(lit + (0 .. n - 1));
    requires \separated(lit + (0 .. n - 1), b,
                         b->dst.ptr + (0 .. b->dst.len - 1));
    terminates \true;
    exits \false;
    assigns b->len, b->overflow,
            b->dst.ptr[0 .. b->dst.len - 1];
    ensures \result <==>
      \old(buf_live(a, *b) && n <= b->dst.len - b->len);
    ensures append_effect{Pre,Post}(b, n, \result);
    ensures \result ==>
      (\forall integer i; 0 <= i < n ==>
        b->dst.ptr[\old(b->len) + i] == \old(lit[i]));
    ensures buf_shape(a, *b);
*/
bool prop_lit_atomic_append(const sc_arena *a, sc_buf *b,
                           const uint8_t *lit, size_t n) {
    return sc_buf_lit(a, b, lit, n);
}

/*@ requires client_buf(a, b);
    requires valid_arena(sa);
    requires slice_storage(sa, s);
    requires \separated(b, sa, sa->base + (0 .. sa->cap - 1));
    requires \separated(sa, b->dst.ptr + (0 .. b->dst.len - 1));
    requires \separated(s.ptr + (0 .. s.len - 1),
                         b->dst.ptr + (0 .. b->dst.len - 1));
    terminates \true;
    exits \false;
    assigns b->len, b->overflow,
            b->dst.ptr[0 .. b->dst.len - 1];
    ensures \result <==>
      \old(buf_live(a, *b) && valid_slice(sa, s) &&
           s.len <= b->dst.len - b->len);
    ensures append_effect{Pre,Post}(b, s.len, \result);
    ensures \result ==>
      (\forall integer i; 0 <= i < s.len ==>
        b->dst.ptr[\old(b->len) + i] == \old(s.ptr[i]));
    ensures buf_shape(a, *b);
*/
bool prop_str_atomic_append(const sc_arena *a, sc_buf *b,
                           const sc_arena *sa, sc_str s) {
    return sc_buf_str(a, b, sa, s);
}

/*@ requires client_buf(a, b);
    terminates \true;
    exits \false;
    assigns b->len, b->overflow,
            b->dst.ptr[0 .. b->dst.len - 1];
    ensures \result <==>
      \old(buf_live(a, *b) && dec_len(v) <= b->dst.len - b->len);
    ensures append_effect{Pre,Post}(b, dec_len(v), \result);
    ensures \result ==> dec_at(b->dst.ptr + \old(b->len), v);
    ensures buf_shape(a, *b);
*/
bool prop_u64_decimal(const sc_arena *a, sc_buf *b, uint64_t v) {
    return sc_buf_u64(a, b, v);
}

/*@ requires client_buf(a, b);
    terminates \true;
    exits \false;
    assigns b->len, b->overflow,
            b->dst.ptr[0 .. b->dst.len - 1];
    ensures \result <==>
      \old(buf_live(a, *b) && sep_len(v) <= b->dst.len - b->len);
    ensures append_effect{Pre,Post}(b, sep_len(v), \result);
    ensures \result ==> sep_at(b->dst.ptr + \old(b->len), v);
    ensures buf_shape(a, *b);
*/
bool prop_u64_grouped(const sc_arena *a, sc_buf *b, uint64_t v) {
    return sc_buf_u64_sep(a, b, v);
}

/*@ requires client_buf(a, b);
    terminates \true;
    exits \false;
    assigns b->len, b->overflow,
            b->dst.ptr[0 .. b->dst.len - 1];
    ensures \result <==>
      \old(buf_live(a, *b) && ms_len(ms) <= b->dst.len - b->len);
    ensures append_effect{Pre,Post}(b, ms_len(ms), \result);
    ensures \result ==> ms_at(b->dst.ptr + \old(b->len), ms);
    ensures buf_shape(a, *b);
*/
bool prop_ms_duration(const sc_arena *a, sc_buf *b, uint64_t ms) {
    return sc_buf_ms(a, b, ms);
}
