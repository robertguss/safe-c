#include "safec/arena.h"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4)
        return 0;
    alignas(8) uint8_t storage[64];
    uint8_t expected[64];
    for (size_t i = 0; i < sizeof storage; i++)
        storage[i] = data[i % size];
    memcpy(expected, storage, sizeof storage);
    sc_arena a;
    sc_arena_init(&a, storage, sizeof storage);
    sc_slice all = sc_arena_alloc(&a, sizeof storage);
    size_t from = data[0] % 65;
    size_t to = data[1] % 65;
    size_t n = data[2] % 65;
    if (n > 64 - from || n > 64 - to)
        return 0;
    sc_slice src = sc_slice_sub(&a, all, from, n);
    size_t dst_len = n + (size_t)data[3] % (65 - to - n);
    sc_slice dst = sc_slice_sub(&a, all, to, dst_len);
    if (data[3] % 2 == 0 && (from + n <= to || to + dst_len <= from)) {
        assert(sc_slice_copy(&a, dst, src));
        if (n > 0)
            memcpy(expected + to, expected + from, n);
    } else {
        assert(sc_slice_move(&a, dst, src));
        if (n > 0)
            memmove(expected + to, expected + from, n);
    }
    assert(memcmp(storage, expected, sizeof storage) == 0);
    uint8_t out = 42;
    assert(!sc_slice_get(&a, src, n, &out) && out == 42);
    if (n > 0)
        assert(sc_slice_get(&a, dst, n - 1, &out) && out == expected[to + n - 1]);
    assert(sc_arena_reset(&a));
    assert(!sc_slice_move(&a, dst, src));
    assert(sc_slice_sub(&a, all, 0, 1).ptr == NULL);
    assert(!sc_slice_get(&a, all, 0, &out));
    return 0;
}
