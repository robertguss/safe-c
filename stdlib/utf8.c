#include "safec/utf8.h"

/*@ requires valid_arena(a) && slice_storage(a, s);
    assigns \nothing;
    ensures \result <==> valid_slice(a, s);
*/
static bool utf8_live(const sc_arena *a, sc_str s) {
    return s.epoch == a->epoch &&
           (s.len == 0 || (s.len <= a->used && s.ptr >= a->base &&
                          s.ptr <= a->base + (a->used - s.len)));
}

/*@ assigns \nothing;
    ensures \result <==> lo <= b <= hi;
*/
static bool in(uint8_t b, uint8_t lo, uint8_t hi) {
    return b >= lo && b <= hi;
}

/* The length of the valid sequence at p, with n bytes readable, or 0. */
/*@ requires n >= 1 && \valid_read(p + (0 .. n - 1));
    assigns \nothing;
    ensures \result == utf8_seq_len(p, n);
    ensures 0 <= \result <= 4 && \result <= n;
*/
static size_t seq_len(const uint8_t *p, size_t n) {
    uint8_t b = p[0];
    if (b <= 0x7F)
        return 1;
    if (b < 0xC2)
        return 0;
    if (b <= 0xDF)
        return n >= 2 && in(p[1], 0x80, 0xBF) ? 2 : 0;
    if (b <= 0xEF) {
        if (n < 3 || !in(p[2], 0x80, 0xBF))
            return 0;
        bool second = b == 0xE0 ? in(p[1], 0xA0, 0xBF)
                      : b == 0xED ? in(p[1], 0x80, 0x9F)
                                  : in(p[1], 0x80, 0xBF);
        return second ? 3 : 0;
    }
    if (b <= 0xF4) {
        if (n < 4 || !in(p[2], 0x80, 0xBF) || !in(p[3], 0x80, 0xBF))
            return 0;
        bool second = b == 0xF0 ? in(p[1], 0x90, 0xBF)
                      : b == 0xF4 ? in(p[1], 0x80, 0x8F)
                                  : in(p[1], 0x80, 0xBF);
        return second ? 4 : 0;
    }
    return 0;
}

/*@ requires 1 <= len <= 4 && \valid_read(p + (0 .. len - 1));
    assigns \nothing;
    ensures \result == utf8_seq_cp(p, len);
*/
static uint32_t seq_cp(const uint8_t *p, size_t len) {
    if (len == 1)
        return p[0];
    if (len == 2)
        return (uint32_t)(p[0] % 32u) * 64u + (uint32_t)(p[1] % 64u);
    if (len == 3)
        return (uint32_t)(p[0] % 16u) * 4096u + (uint32_t)(p[1] % 64u) * 64u +
               (uint32_t)(p[2] % 64u);
    return (uint32_t)(p[0] % 8u) * 262144u + (uint32_t)(p[1] % 64u) * 4096u +
           (uint32_t)(p[2] % 64u) * 64u + (uint32_t)(p[3] % 64u);
}

/*@ requires \valid_read(p + k);
    assigns \nothing;
    ensures \result <==> utf8_boundary(p, k);
*/
static bool boundary(const uint8_t *p, size_t k) {
    return k == 0 || !in(p[k], 0x80, 0xBF);
}

/*@ ghost
  /@ requires n >= 1 && \valid_read(p + (0 .. n - 1));
     assigns \nothing;
     ensures \result == utf8_seq_len(p, n);
     ensures 0 <= \result <= 4 && \result <= n;
  @/
  size_t ghost_seq_len(const uint8_t *p, size_t n) {
    uint8_t b = p[0];
    if (b <= 0x7F) return 1;
    if (b < 0xC2) return 0;
    if (b <= 0xDF) return n >= 2 && p[1] >= 0x80 && p[1] <= 0xBF ? 2 : 0;
    if (b <= 0xEF) {
      if (n < 3 || p[2] < 0x80 || p[2] > 0xBF) return 0;
      if (b == 0xE0) return p[1] >= 0xA0 && p[1] <= 0xBF ? 3 : 0;
      if (b == 0xED) return p[1] >= 0x80 && p[1] <= 0x9F ? 3 : 0;
      return p[1] >= 0x80 && p[1] <= 0xBF ? 3 : 0;
    }
    if (b <= 0xF4) {
      if (n < 4 || p[2] < 0x80 || p[2] > 0xBF || p[3] < 0x80 || p[3] > 0xBF) return 0;
      if (b == 0xF0) return p[1] >= 0x90 && p[1] <= 0xBF ? 4 : 0;
      if (b == 0xF4) return p[1] >= 0x80 && p[1] <= 0x8F ? 4 : 0;
      return p[1] >= 0x80 && p[1] <= 0xBF ? 4 : 0;
    }
    return 0;
  }
*/

/* The induction behind truncate's last promise, as a ghost walk over all n
 * bytes: a valid string's walk steps on every boundary-led sequence start
 * and never over a cut at r, so the prefix up to r is the same walk cut short.
 * The invariants stay exact (b counts the invalid steps so far), since WP
 * cannot prove utf8_bad is never negative without induction. */
/*@ ghost
  /@ requires 0 < r < n && \valid_read(p + (0 .. n - 1));
     requires utf8_boundary(p, r) ||
              (\forall integer k; 0 <= k && r - 3 <= k <= r ==> !utf8_boundary(p, k));
     assigns \nothing;
     ensures utf8_bad(p, n) == 0 ==> utf8_bad(p, r) == 0;
  @/
  void prefix_valid(const uint8_t *p, size_t n, size_t r) {
    size_t j = 0, b = 0;
    /@ loop invariant 0 <= j <= n && 0 <= b <= j;
       loop invariant utf8_bad(p, n) == b + utf8_bad(p + j, n - j);
       loop invariant b == 0 ==>
         (j <= r && utf8_bad(p, r) == utf8_bad(p + j, r - j)) || (j > r && utf8_bad(p, r) == 0);
       loop assigns j, b;
       loop variant n - j;
    @/
    while (j < n) {
      size_t len = ghost_seq_len(p + j, n - j);
      if (len == 0) {
        b++;
        j++;
      } else {
        /@ assert !utf8_cont(p[j]); @/
        /@ assert \forall integer k; j < k < j + len ==> utf8_cont(p[k]); @/
        /@ assert b == 0 && j < r ==> j + len <= r; @/
        /@ assert j + len <= r ==> utf8_seq_len(p + j, r - j) == len; @/
        /@ assert j == r ==> utf8_bad(p + r, 0) == 0; @/
        j += len;
      }
    }
  }
*/

sc_utf8_char sc_utf8_next(const sc_arena *a, sc_str s, size_t i) {
    sc_utf8_char r = {0, 0, false};
    if (!utf8_live(a, s) || i >= s.len)
        return r;
    size_t len = seq_len(s.ptr + i, s.len - i);
    if (len == 0) {
        r.len = 1;
        return r;
    }
    r.cp = seq_cp(s.ptr + i, len);
    r.len = len;
    r.valid = true;
    return r;
}

bool sc_utf8_valid(const sc_arena *a, sc_str s) {
    if (!utf8_live(a, s))
        return false;
    size_t i = 0, bad = 0;
    /*@ loop invariant 0 <= i <= s.len && 0 <= bad <= i;
        loop invariant utf8_bad(s.ptr, s.len) == bad + utf8_bad(s.ptr + i, s.len - i);
        loop assigns i, bad;
        loop variant s.len - i;
    */
    while (i < s.len) {
        size_t len = seq_len(s.ptr + i, s.len - i);
        if (len == 0) {
            bad++;
            len = 1;
        }
        i += len;
    }
    return bad == 0;
}

size_t sc_utf8_count(const sc_arena *a, sc_str s) {
    if (!utf8_live(a, s))
        return 0;
    size_t i = 0, n = 0;
    /*@ loop invariant 0 <= i <= s.len && 0 <= n <= i;
        loop invariant utf8_count(s.ptr, s.len) == n + utf8_count(s.ptr + i, s.len - i);
        loop assigns i, n;
        loop variant s.len - i;
    */
    while (i < s.len) {
        size_t len = seq_len(s.ptr + i, s.len - i);
        i += len > 0 ? len : 1;
        n++;
    }
    return n;
}

size_t sc_utf8_prev(const sc_arena *a, sc_str s, size_t i) {
    if (!utf8_live(a, s) || i == 0 || i > s.len)
        return 0;
    size_t lo = i >= 4 ? i - 4 : 0;
    /*@ loop invariant lo <= j <= i;
        loop invariant \forall integer k; j <= k < i ==> !utf8_boundary(s.ptr, k);
        loop assigns j;
        loop variant j - lo;
    */
    for (size_t j = i; j > lo; j--) {
        if (boundary(s.ptr, j - 1))
            return j - 1;
    }
    return i - 1;
}

sc_str sc_utf8_truncate(const sc_arena *a, sc_str s, size_t max) {
    sc_str empty = {NULL, 0, a->epoch};
    if (!utf8_live(a, s))
        return empty;
    if (max >= s.len)
        return s;
    size_t lo = max >= 3 ? max - 3 : 0;
    sc_str out = {s.ptr, max, s.epoch};
    /*@ loop invariant lo <= j <= max + 1;
        loop invariant \forall integer k; j <= k <= max ==> !utf8_boundary(s.ptr, k);
        loop invariant out.len == max;
        loop assigns j, out.len;
        loop variant j - lo;
    */
    for (size_t j = max + 1; j > lo; j--) {
        if (boundary(s.ptr, j - 1)) {
            out.len = j - 1;
            break;
        }
    }
    /*@ ghost
      if (out.len > 0)
          prefix_valid(s.ptr, s.len, out.len);
    */
    return out;
}
