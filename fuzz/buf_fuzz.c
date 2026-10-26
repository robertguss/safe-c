/* Differential fuzzing of the buf module: decimal output against snprintf,
 * separator output against a reference that inserts commas, durations against
 * a reference written from the brief's rule, and the overflow contract against
 * a model of the builder's length. Ends on the stale path. */
#include "safec/arena.h"
#include "safec/buf.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static size_t ref_sep(uint64_t v, char *out) {
    char plain[32];
    int n = snprintf(plain, sizeof plain, "%llu", (unsigned long long)v);
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        if (i > 0 && (n - i) % 3 == 0)
            out[o++] = ',';
        out[o++] = plain[i];
    }
    return o;
}

static size_t ref_ms(uint64_t ms, char *out) {
    if (ms < 1000)
        return (size_t)snprintf(out, 48, "%llu ms", (unsigned long long)ms);
    uint64_t tenths = ms / 100 + (ms % 100 >= 50);
    if (tenths < 600)
        return (size_t)snprintf(out, 48, "%llu.%llu s", (unsigned long long)(tenths / 10),
                                (unsigned long long)(tenths % 10));
    uint64_t secs = ms / 1000 + (ms % 1000 >= 500);
    return (size_t)snprintf(out, 48, "%llu min %llu s", (unsigned long long)(secs / 60),
                            (unsigned long long)(secs % 60));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    alignas(8) uint8_t store[512], src_store[64];
    sc_arena a, src;
    sc_arena_init(&a, store, sizeof store);
    sc_arena_init(&src, src_store, sizeof src_store);
    if (size < 1)
        return 0;
    sc_slice dst = sc_arena_alloc(&a, 1 + data[0] % 96);
    memset(dst.ptr, '#', dst.len);
    uint8_t mirror[96];
    memset(mirror, '#', sizeof mirror);
    sc_buf b = sc_buf_new(dst);
    size_t len = 0;
    bool over = false;
    for (size_t k = 1; k + 9 <= size; k += 9) {
        uint64_t v = 0;
        memcpy(&v, data + k + 1, 8);
        if (data[k] & 0x10)
            v >>= (data[k] >> 5) * 8; /* reach short numbers too */
        char want[64];
        size_t n;
        bool ok;
        switch (data[k] % 5) {
        case 0:
            n = (size_t)snprintf(want, sizeof want, "%llu", (unsigned long long)v);
            ok = sc_buf_u64(&a, &b, v);
            break;
        case 1:
            n = ref_sep(v, want);
            ok = sc_buf_u64_sep(&a, &b, v);
            break;
        case 2:
            n = ref_ms(v, want);
            ok = sc_buf_ms(&a, &b, v);
            break;
        case 3:
            n = (size_t)(data[k + 1] % 8);
            memcpy(want, data + k + 2, n);
            ok = sc_buf_lit(&a, &b, (const uint8_t *)want, n);
            break;
        default: {
            /* A string from another arena, made stale by a reset now and then. */
            n = (size_t)(data[k + 1] % 8);
            memcpy(want, data + k + 2, n);
            bool stale = (data[k + 1] & 0x80) != 0;
            assert(sc_arena_reset(&src));
            sc_str s = sc_str_from_lit(&src, (const uint8_t *)want, n);
            if (stale)
                assert(sc_arena_reset(&src));
            ok = sc_buf_str(&a, &b, &src, s);
            if (stale) {
                assert(!ok && b.overflow);
                over = true;
                continue;
            }
            break;
        }
        }
        bool fits = !over && n <= dst.len - len;
        assert(ok == fits);
        if (fits) {
            memcpy(mirror + len, want, n);
            len += n;
        } else {
            over = true;
        }
        assert(b.len == len && b.overflow == over);
        assert(memcmp(dst.ptr, mirror, dst.len) == 0);
    }
    sc_str view = sc_buf_view(&a, b);
    assert(view.len == len && view.epoch == a.epoch && (len == 0 || view.ptr == dst.ptr));
    assert(sc_arena_reset(&a));
    view = sc_buf_view(&a, b);
    assert(view.ptr == NULL && view.len == 0);
    sc_buf again = sc_buf_new(dst);
    assert(!sc_buf_u64(&a, &again, 1) && again.overflow);
    return 0;
}
