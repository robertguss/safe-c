#ifndef SAFEC_BUF_H
#define SAFEC_BUF_H

#include "str.h"

/* A byte builder: text appended into a destination slice, front to back, with
 * no printf. `len` bytes of `dst` are written. An append that does not fit, or
 * that finds `dst` stale, writes nothing and sets `overflow`, and every later
 * append fails while it is set, so a label that did not fit is detected rather
 * than cut short in the middle of a number. Read `overflow` and `len` freely;
 * change the builder only through these functions. */
typedef struct {
    sc_slice dst;
    size_t len;
    bool overflow;
} sc_buf;

/*@ logic integer pow10(integer k) = k <= 0 ? 1 : 10 * pow10(k - 1);
    logic integer dec_len(integer v) = v < 10 ? 1 : 1 + dec_len(v / 10);
    logic integer dec_digit(integer v, integer m) = '0' + v / pow10(m) % 10;
    predicate dec_at(uint8_t *p, integer v) =
      \forall integer k; 0 <= k < dec_len(v) ==> p[k] == dec_digit(v, dec_len(v) - 1 - k);
    logic integer sep_len(integer v) = dec_len(v) + (dec_len(v) - 1) / 3;
    predicate sep_at(uint8_t *p, integer v) =
      \forall integer k; 0 <= k < sep_len(v) ==>
        ((sep_len(v) - 1 - k) % 4 == 3 ?
           p[k] == ',' :
           p[k] == dec_digit(v, (sep_len(v) - 1 - k) - (sep_len(v) - 1 - k) / 4));
    logic integer ms_tenths(integer ms) = ms / 100 + (ms % 100 >= 50 ? 1 : 0);
    logic integer ms_secs(integer ms) = ms / 1000 + (ms % 1000 >= 500 ? 1 : 0);
    logic integer ms_len(integer ms) =
      ms < 1000 ? dec_len(ms) + 3 :
      ms_tenths(ms) < 600 ? dec_len(ms_tenths(ms) / 10) + 4 :
      dec_len(ms_secs(ms) / 60) + 5 + dec_len(ms_secs(ms) % 60) + 2;
    predicate ms_at(uint8_t *p, integer ms) =
      ms < 1000 ?
        (\let n = dec_len(ms);
         dec_at(p, ms) && p[n] == ' ' && p[n + 1] == 'm' && p[n + 2] == 's') :
      ms_tenths(ms) < 600 ?
        (\let n = dec_len(ms_tenths(ms) / 10);
         dec_at(p, ms_tenths(ms) / 10) && p[n] == '.' && p[n + 1] == '0' + ms_tenths(ms) % 10 &&
         p[n + 2] == ' ' && p[n + 3] == 's') :
        (\let n = dec_len(ms_secs(ms) / 60);
         \let r = dec_len(ms_secs(ms) % 60);
         dec_at(p, ms_secs(ms) / 60) && p[n] == ' ' && p[n + 1] == 'm' && p[n + 2] == 'i' &&
         p[n + 3] == 'n' && p[n + 4] == ' ' && dec_at(p + (n + 5), ms_secs(ms) % 60) &&
         p[n + 5 + r] == ' ' && p[n + 6 + r] == 's');
    predicate buf_shape(sc_arena *a, sc_buf b) =
      slice_storage(a, b.dst) && b.len <= b.dst.len;
    predicate buf_live(sc_arena *a, sc_buf b) =
      valid_slice(a, b.dst) && b.len <= b.dst.len && !b.overflow;
*/

/* A builder over `dst`, empty and not overflowed. */
/*@ terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result.dst.ptr == dst.ptr && \result.dst.len == dst.len &&
            \result.dst.epoch == dst.epoch;
    ensures \result.len == 0 && !\result.overflow;
*/
sc_buf sc_buf_new(sc_slice dst);

/* The written prefix of `b` as a string in `a`; the canonical empty string
 * when `dst` is stale or nothing is written. */
/*@ requires valid_arena(a) && buf_shape(a, b);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result.epoch == a->epoch;
    behavior written:
      assumes valid_slice(a, b.dst) && b.len > 0;
      ensures \result.ptr == b.dst.ptr && \result.len == b.len;
      ensures valid_slice(a, \result);
    behavior empty:
      assumes !(valid_slice(a, b.dst) && b.len > 0);
      ensures \result.ptr == \null && \result.len == 0;
    complete behaviors;
    disjoint behaviors;
*/
sc_str sc_buf_view(const sc_arena *a, sc_buf b);

/* Appends the n bytes at `lit`. The caller guarantees lit is readable for n
 * bytes and does not overlap the builder. Use SC_BUF_LIT for a literal. */
/*@
    requires valid_arena(a) && \valid(b) && buf_shape(a, *b);
    requires \separated(b, a, a->base + (0 .. a->cap - 1));
    requires n == 0 || \valid_read(lit + (0 .. n - 1));
    requires n == 0 || \separated(lit + (0 .. n - 1), b, b->dst.ptr + (0 .. b->dst.len - 1));
    terminates \true;
    exits \false;
    assigns b->len, b->overflow, b->dst.ptr[b->len .. b->dst.len - 1];
    ensures \result ==> \forall integer k; \old(b->len) + n <= k < b->dst.len ==> b->dst.ptr[k] == \old(b->dst.ptr[k]);
    ensures buf_shape(a, *b);
    ensures \result <==> \old(buf_live(a, *b) && n <= b->dst.len - b->len);
    ensures \result ==> b->len == \old(b->len) + n && !b->overflow;
    ensures !\result ==> b->len == \old(b->len) && b->overflow;
    ensures !\result ==> \forall integer k; 0 <= k < b->dst.len ==> b->dst.ptr[k] == \old(b->dst.ptr[k]);
    ensures \result ==> \forall integer k; 0 <= k < n ==> b->dst.ptr[\old(b->len) + k] == lit[k];
*/
bool sc_buf_lit(const sc_arena *a, sc_buf *b, const uint8_t *lit, size_t n);

#define SC_BUF_LIT(a, b, lit) sc_buf_lit((a), (b), (const uint8_t *)(lit), sizeof(lit) - 1)

/* Appends the bytes of `s`, which lives in `sa` (the same arena as the
 * builder, or another one). A stale `s` fails like a full builder. */
/*@
    requires valid_arena(a) && \valid(b) && buf_shape(a, *b);
    requires \separated(b, a, a->base + (0 .. a->cap - 1));
    requires valid_arena(sa) && slice_storage(sa, s);
    requires \separated(b, sa, sa->base + (0 .. sa->cap - 1));
    requires \separated(sa, b->dst.ptr + (0 .. b->dst.len - 1));
    requires s.len == 0 || \separated(s.ptr + (0 .. s.len - 1), b->dst.ptr + (0 .. b->dst.len - 1));
    terminates \true;
    exits \false;
    assigns b->len, b->overflow, b->dst.ptr[b->len .. b->dst.len - 1];
    ensures \result ==> \forall integer k; \old(b->len) + s.len <= k < b->dst.len ==> b->dst.ptr[k] == \old(b->dst.ptr[k]);
    ensures buf_shape(a, *b);
    ensures \result <==> \old(buf_live(a, *b) && valid_slice(sa, s) && s.len <= b->dst.len - b->len);
    ensures \result ==> b->len == \old(b->len) + s.len && !b->overflow;
    ensures !\result ==> b->len == \old(b->len) && b->overflow;
    ensures !\result ==> \forall integer k; 0 <= k < b->dst.len ==> b->dst.ptr[k] == \old(b->dst.ptr[k]);
    ensures \result ==> \forall integer k; 0 <= k < s.len ==> b->dst.ptr[\old(b->len) + k] == s.ptr[k];
*/
bool sc_buf_str(const sc_arena *a, sc_buf *b, const sc_arena *sa, sc_str s);

/* Appends `v` in decimal: "0", "1000", "18446744073709551615". */
/*@
    requires valid_arena(a) && \valid(b) && buf_shape(a, *b);
    requires \separated(b, a, a->base + (0 .. a->cap - 1));
    terminates \true;
    exits \false;
    assigns b->len, b->overflow, b->dst.ptr[b->len .. b->dst.len - 1];
    ensures \result ==> \forall integer k; \old(b->len) + dec_len(v) <= k < b->dst.len ==> b->dst.ptr[k] == \old(b->dst.ptr[k]);
    ensures buf_shape(a, *b);
    ensures \result <==> \old(buf_live(a, *b) && dec_len(v) <= b->dst.len - b->len);
    ensures \result ==> b->len == \old(b->len) + dec_len(v) && !b->overflow;
    ensures !\result ==> b->len == \old(b->len) && b->overflow;
    ensures !\result ==> \forall integer k; 0 <= k < b->dst.len ==> b->dst.ptr[k] == \old(b->dst.ptr[k]);
    ensures \result ==> dec_at(b->dst.ptr + \old(b->len), v);
*/
bool sc_buf_u64(const sc_arena *a, sc_buf *b, uint64_t v);

/* Appends `v` in decimal with a comma between each group of three digits,
 * counted from the right: "999", "1,000", "182,334". */
/*@
    requires valid_arena(a) && \valid(b) && buf_shape(a, *b);
    requires \separated(b, a, a->base + (0 .. a->cap - 1));
    terminates \true;
    exits \false;
    assigns b->len, b->overflow, b->dst.ptr[b->len .. b->dst.len - 1];
    ensures \result ==> \forall integer k; \old(b->len) + sep_len(v) <= k < b->dst.len ==> b->dst.ptr[k] == \old(b->dst.ptr[k]);
    ensures buf_shape(a, *b);
    ensures \result <==> \old(buf_live(a, *b) && sep_len(v) <= b->dst.len - b->len);
    ensures \result ==> b->len == \old(b->len) + sep_len(v) && !b->overflow;
    ensures !\result ==> b->len == \old(b->len) && b->overflow;
    ensures !\result ==> \forall integer k; 0 <= k < b->dst.len ==> b->dst.ptr[k] == \old(b->dst.ptr[k]);
    ensures \result ==> sep_at(b->dst.ptr + \old(b->len), v);
*/
bool sc_buf_u64_sep(const sc_arena *a, sc_buf *b, uint64_t v);

/* Appends a duration of `ms` milliseconds:
 *   under 1000 ms:              "850 ms"
 *   under 59.95 s:              seconds to one decimal, half up: "1.0 s", "1.1 s", "59.9 s"
 *   otherwise:                  whole seconds, half up, as minutes and seconds: "1 min 0 s" */
/*@
    requires valid_arena(a) && \valid(b) && buf_shape(a, *b);
    requires \separated(b, a, a->base + (0 .. a->cap - 1));
    terminates \true;
    exits \false;
    assigns b->len, b->overflow, b->dst.ptr[b->len .. b->dst.len - 1];
    ensures \result ==> \forall integer k; \old(b->len) + ms_len(ms) <= k < b->dst.len ==> b->dst.ptr[k] == \old(b->dst.ptr[k]);
    ensures buf_shape(a, *b);
    ensures \result <==> \old(buf_live(a, *b) && ms_len(ms) <= b->dst.len - b->len);
    ensures \result ==> b->len == \old(b->len) + ms_len(ms) && !b->overflow;
    ensures !\result ==> b->len == \old(b->len) && b->overflow;
    ensures !\result ==> \forall integer k; 0 <= k < b->dst.len ==> b->dst.ptr[k] == \old(b->dst.ptr[k]);
    ensures \result ==> ms_at(b->dst.ptr + \old(b->len), ms);
*/
bool sc_buf_ms(const sc_arena *a, sc_buf *b, uint64_t ms);

#endif
