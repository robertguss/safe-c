#include "safec/utf8.h"

static bool utf8_live(const sc_arena *a, sc_str s) {
    return s.epoch == a->epoch &&
           (s.len == 0 || (s.len <= a->used && s.ptr >= a->base &&
                          s.ptr <= a->base + (a->used - s.len)));
}

static bool in(uint8_t b, uint8_t lo, uint8_t hi) {
    return b >= lo && b <= hi;
}

/* The length of the valid sequence at p, with n bytes readable, or 0. */
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

static bool boundary(const uint8_t *p, size_t k) {
    return k == 0 || !in(p[k], 0x80, 0xBF);
}

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
    size_t i = 0;
    while (i < s.len) {
        size_t len = seq_len(s.ptr + i, s.len - i);
        if (len == 0)
            return false;
        i += len;
    }
    return true;
}

size_t sc_utf8_count(const sc_arena *a, sc_str s) {
    if (!utf8_live(a, s))
        return 0;
    size_t i = 0, n = 0;
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
    for (size_t j = max + 1; j > lo; j--) {
        if (boundary(s.ptr, j - 1)) {
            out.len = j - 1;
            return out;
        }
    }
    return out;
}
