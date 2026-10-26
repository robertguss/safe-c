#ifndef SAFEC_UTF8_H
#define SAFEC_UTF8_H

#include "str.h"

/* UTF-8 over byte strings, as RFC 3629 defines it: decode one sequence,
 * validate, count, step back one character and cut at a character boundary.
 * Nothing allocates and nothing reads past a string. A stale string gives the
 * same defined result as an empty one where the function says so. */

/* One sequence read by sc_utf8_next. */
typedef struct {
    uint32_t cp; /* the code point; 0 unless valid */
    size_t len;  /* bytes to step over: 1 to 4 when valid, 1 when invalid, 0 at the end */
    bool valid;
} sc_utf8_char;

/*@ predicate utf8_cont(integer b) = 0x80 <= b <= 0xBF;
    logic integer utf8_seq_len(uint8_t *p, integer n) =
      n < 1 ? 0 :
      p[0] <= 0x7F ? 1 :
      p[0] < 0xC2 ? 0 :
      p[0] <= 0xDF ? (n >= 2 && 0x80 <= p[1] && p[1] <= 0xBF ? 2 : 0) :
      p[0] <= 0xEF ?
        (n >= 3 && 0x80 <= p[2] && p[2] <= 0xBF &&
         (p[0] == 0xE0 ? 0xA0 <= p[1] && p[1] <= 0xBF :
          p[0] == 0xED ? 0x80 <= p[1] && p[1] <= 0x9F :
                         0x80 <= p[1] && p[1] <= 0xBF) ? 3 : 0) :
      p[0] <= 0xF4 ?
        (n >= 4 && 0x80 <= p[2] && p[2] <= 0xBF && 0x80 <= p[3] && p[3] <= 0xBF &&
         (p[0] == 0xF0 ? 0x90 <= p[1] && p[1] <= 0xBF :
          p[0] == 0xF4 ? 0x80 <= p[1] && p[1] <= 0x8F :
                         0x80 <= p[1] && p[1] <= 0xBF) ? 4 : 0) :
      0;
    logic integer utf8_seq_cp(uint8_t *p, integer len) =
      len == 1 ? p[0] :
      len == 2 ? (p[0] % 32) * 64 + p[1] % 64 :
      len == 3 ? (p[0] % 16) * 4096 + (p[1] % 64) * 64 + p[2] % 64 :
      len == 4 ? (p[0] % 8) * 262144 + (p[1] % 64) * 4096 + (p[2] % 64) * 64 + p[3] % 64 :
      0;
    logic integer utf8_step(uint8_t *p, integer n) =
      utf8_seq_len(p, n) > 0 ? utf8_seq_len(p, n) : 1;
    logic integer utf8_count(uint8_t *p, integer n) =
      n <= 0 ? 0 : 1 + utf8_count(p + utf8_step(p, n), n - utf8_step(p, n));
    logic integer utf8_bad(uint8_t *p, integer n) =
      n <= 0 ? 0 :
      (utf8_seq_len(p, n) == 0 ? 1 : 0) + utf8_bad(p + utf8_step(p, n), n - utf8_step(p, n));
    predicate utf8_boundary(uint8_t *p, integer k) = k == 0 || !utf8_cont(p[k]);
*/

/* The sequence that starts at byte `i` of `s`: a valid one gives its code
 * point and length; an invalid one, or one cut short by the end of `s`, gives
 * length 1 (the byte to skip); `i` at or past the end, or a stale `s`, gives
 * length 0. Never reads past `s`. */
/*@ requires valid_arena(a) && slice_storage(a, s);
    terminates \true;
    exits \false;
    assigns \nothing;
    behavior none:
      assumes !valid_slice(a, s) || i >= s.len;
      ensures \result.len == 0 && \result.cp == 0 && !\result.valid;
    behavior bad:
      assumes valid_slice(a, s) && i < s.len && utf8_seq_len(s.ptr + i, s.len - i) == 0;
      ensures \result.len == 1 && \result.cp == 0 && !\result.valid;
    behavior good:
      assumes valid_slice(a, s) && i < s.len && utf8_seq_len(s.ptr + i, s.len - i) > 0;
      ensures \result.valid && \result.len == utf8_seq_len(s.ptr + i, s.len - i);
      ensures \result.cp == utf8_seq_cp(s.ptr + i, \result.len);
    complete behaviors;
    disjoint behaviors;
*/
sc_utf8_char sc_utf8_next(const sc_arena *a, sc_str s, size_t i);

/* Whether `s` is live and holds only valid sequences. A live empty string is
 * valid. */
/*@ requires valid_arena(a) && slice_storage(a, s);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result <==> valid_slice(a, s) && utf8_bad(s.ptr, s.len) == 0;
*/
bool sc_utf8_valid(const sc_arena *a, sc_str s);

/* The steps a walk over `s` with sc_utf8_next takes: its code points, with
 * each invalid byte counted as one. 0 for a stale `s`. */
/*@ requires valid_arena(a) && slice_storage(a, s);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures valid_slice(a, s) ==> \result == utf8_count(s.ptr, s.len);
    ensures !valid_slice(a, s) ==> \result == 0;
*/
size_t sc_utf8_count(const sc_arena *a, sc_str s);

/* The start of the character before offset `i`, for backspace: the largest
 * boundary in i - 4 .. i - 1, or i - 1 when none of them is one. 0 when `i`
 * is 0 or past the end, or `s` is stale. */
/*@ requires valid_arena(a) && slice_storage(a, s);
    terminates \true;
    exits \false;
    assigns \nothing;
    behavior none:
      assumes !valid_slice(a, s) || i == 0 || i > s.len;
      ensures \result == 0;
    behavior back:
      assumes valid_slice(a, s) && 0 < i <= s.len;
      ensures i - 4 <= \result < i;
      ensures \forall integer k; \result < k < i ==> !utf8_boundary(s.ptr, k);
      ensures utf8_boundary(s.ptr, \result) ||
              (\result == i - 1 &&
               \forall integer k; 0 <= k && i - 4 <= k < i ==> !utf8_boundary(s.ptr, k));
    complete behaviors;
    disjoint behaviors;
*/
size_t sc_utf8_prev(const sc_arena *a, sc_str s, size_t i);

/* `s` cut to at most `max` bytes at a character boundary: `s` itself when it
 * fits, else its prefix ending at the largest boundary in max - 3 .. max, or
 * at `max` when none of them is one. A prefix of a valid string is valid. The
 * prefix shares `s`'s pointer and epoch; a stale `s` gives the canonical
 * empty string. */
/*@ requires valid_arena(a) && slice_storage(a, s);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures valid_slice(a, s) ==> valid_slice(a, \result);
    ensures valid_slice(a, s) && utf8_bad(s.ptr, s.len) == 0 ==>
            utf8_bad(\result.ptr, \result.len) == 0;
    behavior stale:
      assumes !valid_slice(a, s);
      ensures \result.ptr == \null && \result.len == 0 && \result.epoch == a->epoch;
    behavior whole:
      assumes valid_slice(a, s) && max >= s.len;
      ensures \result.ptr == s.ptr && \result.len == s.len && \result.epoch == s.epoch;
    behavior cut:
      assumes valid_slice(a, s) && max < s.len;
      ensures \result.ptr == s.ptr && \result.epoch == s.epoch;
      ensures max - 3 <= \result.len <= max;
      ensures \forall integer k; \result.len < k <= max ==> !utf8_boundary(s.ptr, k);
      ensures utf8_boundary(s.ptr, \result.len) ||
              (\result.len == max &&
               \forall integer k; 0 <= k && max - 3 <= k <= max ==> !utf8_boundary(s.ptr, k));
    complete behaviors;
    disjoint behaviors;
*/
sc_str sc_utf8_truncate(const sc_arena *a, sc_str s, size_t max);

#endif
