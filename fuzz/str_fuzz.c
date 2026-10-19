#include "safec/arena.h"
#include "safec/str.h"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

/* Naive reference: first offset where needle matches, or -1. */
static long ref_find(const uint8_t *h, size_t hn, const uint8_t *n, size_t nn) {
    if (nn > hn)
        return -1;
    for (size_t i = 0; i + nn <= hn; i++) {
        if (memcmp(h + i, n, nn) == 0)
            return (long)i;
    }
    return -1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 3)
        return 0;
    size_t hn = data[0] % 33;
    size_t nn = data[1] % 9;
    uint8_t mode = data[2];
    const uint8_t *body = data + 3;
    size_t blen = size - 3;
    uint8_t hbuf[32];
    uint8_t nbuf[8];
    for (size_t i = 0; i < hn; i++)
        hbuf[i] = blen > 0 ? (uint8_t)(body[i % blen] & 3) : 0;
    for (size_t i = 0; i < nn; i++)
        nbuf[i] = blen > 0 ? (uint8_t)(body[(i * 7 + 1) % blen] & 3) : 0;
    if (mode % 2 == 1 && nn <= hn && nn > 0)
        memcpy(nbuf, hbuf + (mode / 2) % (hn - nn + 1), nn);

    alignas(8) uint8_t storage[64];
    sc_arena a;
    sc_arena_init(&a, storage, sizeof storage);
    sc_str h = sc_str_from_lit(&a, hbuf, hn);
    sc_str n = sc_str_from_lit(&a, nbuf, nn);
    assert(h.len == hn && n.len == nn);
    assert(hn == 0 || memcmp(h.ptr, hbuf, hn) == 0);
    assert(nn == 0 || memcmp(n.ptr, nbuf, nn) == 0);

    long want = ref_find(hbuf, hn, nbuf, nn);
    size_t at = 1234;
    bool found = sc_str_find(&a, h, n, &at);
    assert(found == (want >= 0));
    assert(found ? (long)at == want : at == 1234);

    bool same = hn == nn && (hn == 0 || memcmp(hbuf, nbuf, hn) == 0);
    assert(sc_str_eq(&a, h, n) == same);
    assert(sc_str_eq(&a, h, h));
    assert(sc_str_eq_lit(&a, h, hbuf, hn));
    assert(sc_str_eq_lit(&a, n, nbuf, nn));
    assert(sc_str_eq_lit(&a, h, nbuf, nn) == same);

    assert(sc_arena_reset(&a));
    at = 1234;
    assert(!sc_str_eq(&a, h, h));
    assert(!sc_str_eq_lit(&a, h, hbuf, hn));
    assert(!sc_str_find(&a, h, n, &at) && at == 1234);
    return 0;
}
