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

/* The sequence that starts at byte `i` of `s`. */
sc_utf8_char sc_utf8_next(const sc_arena *a, sc_str s, size_t i);

/* Whether `s` is live and holds only valid sequences. */
bool sc_utf8_valid(const sc_arena *a, sc_str s);

/* The steps a walk over `s` with sc_utf8_next takes. */
size_t sc_utf8_count(const sc_arena *a, sc_str s);

/* The start of the character before offset `i`. */
size_t sc_utf8_prev(const sc_arena *a, sc_str s, size_t i);

/* `s` cut to at most `max` bytes at a character boundary. */
sc_str sc_utf8_truncate(const sc_arena *a, sc_str s, size_t max);

#endif
