#include "safec/arena.h"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    alignas(8) uint8_t storage[64] = {0};
    sc_arena a;
    size_t cap = size == 0 ? 0 : (size_t)data[0] % 65;
    sc_arena_init(&a, storage, cap);
    size_t used = 0;
    for (size_t i = 1; i < size; i++) {
        if (data[i] % 4 == 0) {
            sc_slice stale = sc_arena_alloc(&a, 1);
            assert(sc_arena_reset(&a));
            uint8_t out = 0;
            assert(!sc_slice_get(&a, stale, 0, &out));
            used = 0;
        } else {
            size_t n = data[i] == 255 ? SIZE_MAX : data[i] % 80;
            sc_slice s = sc_arena_alloc(&a, n);
            if (n > 0 && n <= cap - used && ((n + 7) & ~(size_t)7) <= cap - used) {
                assert(s.ptr == storage + used && s.len == n);
                assert((uintptr_t)s.ptr % 8 == 0);
                memset(s.ptr, data[i], n);
                used += (n + 7) & ~(size_t)7;
            } else {
                assert(s.ptr == NULL && s.len == 0);
            }
        }
        assert(a.used == used && sc_arena_is_valid(&a));
    }
    return 0;
}
