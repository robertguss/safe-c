#include "safec/arena.h"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

int main(void) {
    alignas(8) uint8_t storage[64] = {0};
    alignas(8) uint8_t other_storage[8] = {0};
    sc_arena a, other;
    sc_arena_init(&a, storage, sizeof storage);
    sc_arena_init(&other, other_storage, sizeof other_storage);
    sc_slice empty = sc_arena_alloc(&a, 0);
    uint8_t out = 123;
    assert(!sc_slice_get(&a, empty, 0, &out) && out == 123);
    assert(sc_slice_sub(&a, empty, 0, 0).ptr == NULL);
    assert(sc_slice_copy(&a, empty, empty));
    assert(sc_slice_move(&a, empty, empty));

    sc_slice all = sc_arena_alloc(&a, 32);
    for (size_t i = 0; i < all.len; i++)
        all.ptr[i] = (uint8_t)i;
    assert(sc_slice_get(&a, all, 31, &out) && out == 31);
    assert(!sc_slice_get(&a, all, 32, &out) && out == 31);
    assert(!sc_slice_get(&a, all, SIZE_MAX, &out) && out == 31);
    assert(sc_slice_sub(&a, all, 32, 0).ptr == NULL);
    assert(sc_slice_sub(&a, all, SIZE_MAX, 1).ptr == NULL);
    assert(sc_slice_sub(&a, all, 1, SIZE_MAX).ptr == NULL);
    sc_slice src = sc_slice_sub(&a, all, 0, 8);
    sc_slice dst = sc_slice_sub(&a, all, 16, 8);
    assert(dst.ptr == all.ptr + 16 && dst.len == 8);
    assert(sc_slice_copy(&a, dst, src) && memcmp(dst.ptr, src.ptr, 8) == 0);
    uint8_t before[32];
    memcpy(before, all.ptr, 32);
    assert(!sc_slice_copy(&a, sc_slice_sub(&a, dst, 0, 2), src));
    assert(!sc_slice_move(&a, sc_slice_sub(&a, dst, 0, 2), src));
    assert(memcmp(before, all.ptr, 32) == 0);

    memmove(before + 3, before, 12);
    assert(sc_slice_move(&a, sc_slice_sub(&a, all, 3, 12), sc_slice_sub(&a, all, 0, 12)));
    assert(memcmp(before, all.ptr, 32) == 0);
    memmove(before, before + 3, 12);
    assert(sc_slice_move(&a, sc_slice_sub(&a, all, 0, 12), sc_slice_sub(&a, all, 3, 12)));
    assert(memcmp(before, all.ptr, 32) == 0);
    assert(sc_slice_move(&a, all, all));
    assert(sc_arena_reset(&other));
    assert(sc_slice_get(&a, all, 0, &out));
    assert(sc_arena_reset(&a));
    assert(!sc_slice_get(&a, all, 0, &out));
    assert(sc_slice_sub(&a, all, 0, 1).ptr == NULL);
    assert(!sc_slice_copy(&a, dst, src));
    assert(!sc_slice_move(&a, dst, src));
    sc_slice fresh = sc_arena_alloc(&a, 32);
    assert(fresh.ptr == all.ptr);
    out = 123;
    assert(!sc_slice_get(&a, all, 0, &out) && out == 123);
    assert(sc_slice_get(&a, fresh, 0, &out));
    assert(!sc_slice_copy(&a, fresh, src));
    assert(!sc_slice_move(&a, fresh, src));
    return 0;
}
