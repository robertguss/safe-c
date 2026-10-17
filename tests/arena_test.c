#include "safec/arena.h"
#include <assert.h>
#include <stdalign.h>

int main(void) {
    alignas(8) uint8_t storage[40] = {0};
    sc_arena a;
    assert(!sc_arena_is_valid(NULL));
    sc_arena_init(&a, NULL, 0);
    assert(sc_arena_is_valid(&a));
    assert(sc_arena_alloc(&a, 1).ptr == NULL);

    sc_arena_init(&a, storage, sizeof storage);
    assert(sc_arena_is_valid(&a) && a.used == 0 && a.epoch == 0);
    sc_slice empty = sc_arena_alloc(&a, 0);
    assert(empty.ptr == NULL && empty.len == 0 && a.used == 0);
    sc_slice first = sc_arena_alloc(&a, 1);
    sc_slice second = sc_arena_alloc(&a, 9);
    assert(first.ptr == storage && first.len == 1 && a.used == 24);
    assert(second.ptr == storage + 8 && second.len == 9);
    assert((uintptr_t)first.ptr % 8 == 0 && (uintptr_t)second.ptr % 8 == 0);
    assert(sc_arena_alloc(&a, SIZE_MAX).ptr == NULL && a.used == 24);
    assert(sc_arena_alloc(&a, 17).ptr == NULL && a.used == 24);
    assert(sc_arena_alloc(&a, 16).ptr == storage + 24 && a.used == 40);
    assert(sc_arena_alloc(&a, 1).ptr == NULL && a.used == 40);
    assert(sc_arena_reset(&a) && a.used == 0 && a.epoch == 1);
    sc_slice reused = sc_arena_alloc(&a, 1);
    assert(reused.ptr == first.ptr && reused.epoch != first.epoch);

    sc_arena_init(&a, storage, 9);
    assert(sc_arena_alloc(&a, 1).len == 1 && a.used == 8);
    assert(sc_arena_alloc(&a, 1).ptr == NULL && a.used == 8);
    a.epoch = UINT_MAX;
    assert(!sc_arena_reset(&a) && a.epoch == UINT_MAX && a.used == 8);
    a.used = 9;
    assert(!sc_arena_is_valid(&a));
    a.used = 16;
    assert(!sc_arena_is_valid(&a));
    return 0;
}
