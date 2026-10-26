/* Differential fuzzing of the utf8 module against a reference decoder written
 * from RFC 3629 a different way: it assembles the code point from the lead
 * byte's length class first, then rejects overlong forms, surrogates and
 * values past U+10FFFF by value, where stdlib/utf8.c checks byte ranges.
 * Every offset is decoded; valid, count, prev and truncate are checked against
 * the reference walk, and truncate of a valid string must pass the reference walk. Ends on
 * the stale path. */
#include "safec/arena.h"
#include "safec/utf8.h"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

/* The valid sequence at p (n bytes left): its length, or 0. */
static size_t ref_len(const uint8_t *p, size_t n, uint32_t *cp) {
    uint8_t b = p[0];
    size_t len = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 0;
    if (len == 0 || len > n)
        return 0;
    uint32_t v = len == 1 ? b : len == 2 ? (b & 0x1Fu) : len == 3 ? (b & 0x0Fu) : (b & 0x07u);
    for (size_t k = 1; k < len; k++) {
        if ((p[k] & 0xC0u) != 0x80u)
            return 0;
        v = (v << 6) | (p[k] & 0x3Fu);
    }
    static const uint32_t least[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (v < least[len] || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
        return 0;
    *cp = v;
    return len;
}

static bool is_boundary(const uint8_t *p, size_t k) {
    return k == 0 || (p[k] & 0xC0u) != 0x80u;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    alignas(8) uint8_t store[1024];
    sc_arena a;
    memset(store, 0x80, sizeof store); /* a read past the string sees a continuation byte */
    sc_arena_init(&a, store, sizeof store);
    if (size < 1 || size > 512)
        return 0;
    size_t max = data[0];
    sc_str s = sc_str_from_lit(&a, data + 1, size - 1);
    const uint8_t *p = data + 1;
    size_t n = size - 1;
    if (n == 0)
        s = (sc_str){NULL, 0, a.epoch};

    for (size_t i = 0; i <= n + 1; i++) {
        sc_utf8_char c = sc_utf8_next(&a, s, i);
        if (i >= n) {
            assert(c.len == 0 && !c.valid && c.cp == 0);
            continue;
        }
        uint32_t cp = 0;
        size_t len = ref_len(p + i, n - i, &cp);
        assert(c.valid == (len > 0));
        assert(c.len == (len > 0 ? len : 1));
        assert(c.cp == (len > 0 ? cp : 0));
    }

    size_t steps = 0, bad = 0;
    for (size_t i = 0; i < n; steps++) {
        uint32_t cp;
        size_t len = ref_len(p + i, n - i, &cp);
        bad += len == 0;
        i += len > 0 ? len : 1;
    }
    bool valid = sc_utf8_valid(&a, s);
    assert(valid == (bad == 0));
    assert(sc_utf8_count(&a, s) == steps);

    for (size_t i = 0; i <= n + 1; i++) {
        size_t j = sc_utf8_prev(&a, s, i);
        if (i == 0 || i > n) {
            assert(j == 0);
            continue;
        }
        size_t want = i - 1;
        for (size_t k = i; k > 0 && k + 4 > i; k--) {
            if (is_boundary(p, k - 1)) {
                want = k - 1;
                break;
            }
        }
        assert(j == want);
    }

    sc_str t = sc_utf8_truncate(&a, s, max);
    assert(t.ptr == s.ptr && t.epoch == s.epoch);
    if (max >= n) {
        assert(t.len == n);
    } else {
        size_t want = max;
        for (size_t k = max + 1; k > 0 && k + 3 > max; k--) {
            if (is_boundary(p, k - 1)) {
                want = k - 1;
                break;
            }
        }
        assert(t.len == want);
    }
    if (valid) {
        for (size_t i = 0; i < t.len;) {
            uint32_t cp;
            size_t len = ref_len(p + i, t.len - i, &cp);
            assert(len > 0);
            i += len;
        }
    }

    for (size_t far = SIZE_MAX - 1u; far != 0; far++) { /* SIZE_MAX - 1, SIZE_MAX */
        assert(sc_utf8_next(&a, s, far).len == 0 && sc_utf8_prev(&a, s, far) == 0);
        assert(sc_utf8_truncate(&a, s, far).len == n);
    }

    assert(sc_arena_reset(&a));
    assert(!sc_utf8_valid(&a, s) && sc_utf8_count(&a, s) == 0);
    assert(sc_utf8_next(&a, s, 0).len == 0 && sc_utf8_prev(&a, s, n) == 0);
    t = sc_utf8_truncate(&a, s, max);
    assert(t.ptr == NULL && t.len == 0 && t.epoch == a.epoch);
    return 0;
}
