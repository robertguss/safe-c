#include "safec/arena.h"
#include "safec/handle.h"
#include <assert.h>
#include <stdalign.h>
#include <stdint.h>

static bool is_live(const sc_arena *a, sc_handle_table t, sc_handle h) {
    sc_option_size o = sc_handle_get(a, t, h);
    if (!sc_option_size_is_some(o)) {
        assert(o.value == 0);
        return false;
    }
    assert(sc_option_size_unwrap(o) == h.index);
    return true;
}

static void test_new(void) {
    alignas(8) uint8_t storage[32];
    sc_arena a;
    sc_arena_init(&a, storage, sizeof storage);
    for (size_t i = 0; i < sizeof storage; i++)
        storage[i] = 0xAA;

    /* Zero, oversized and unfitting capacities give the empty table and leave
     * the arena untouched. 3 * 11 = 33 bytes does not fit in 32. */
    const size_t bad[] = {0, 11, SIZE_MAX / 3 + 1, SIZE_MAX};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        sc_handle_table t = sc_handle_table_new(&a, bad[i]);
        assert(t.cap == 0 && t.slots.ptr == NULL && t.slots.len == 0);
        assert(t.slots.epoch == a.epoch && a.used == 0);
        sc_handle out = {77, 77};
        assert(!sc_handle_alloc(&a, t, &out));
        assert(out.index == 77 && out.generation == 77);
    }

    /* The largest fitting table: 10 slots, 30 bytes rounded to 32, all zeroed. */
    sc_handle_table t = sc_handle_table_new(&a, 10);
    assert(t.cap == 10 && t.slots.len == 30 && t.slots.ptr == storage);
    assert(a.used == 32);
    for (size_t i = 0; i < 30; i++)
        assert(storage[i] == 0);
    assert(storage[30] == 0xAA && storage[31] == 0xAA);
    assert(sc_handle_table_new(&a, 1).cap == 0 && a.used == 32);
}

static void test_alloc_free_reuse_full(void) {
    alignas(8) uint8_t storage[64];
    sc_arena a;
    sc_arena_init(&a, storage, sizeof storage);
    sc_handle_table t = sc_handle_table_new(&a, 4);
    assert(t.cap == 4);

    /* Alloc issues the lowest free slot at generation 0 until the table is full. */
    sc_handle h[4];
    for (size_t i = 0; i < 4; i++) {
        assert(sc_handle_alloc(&a, t, &h[i]));
        assert(h[i].index == i && h[i].generation == 0);
        assert(is_live(&a, t, h[i]));
    }
    sc_handle out = {99, 99};
    assert(!sc_handle_alloc(&a, t, &out));
    assert(out.index == 99 && out.generation == 99);

    /* A freed handle returns none on the next get, and a second free fails. */
    assert(sc_handle_free(&a, t, h[2]));
    assert(!is_live(&a, t, h[2]));
    assert(!sc_handle_free(&a, t, h[2]));
    assert(is_live(&a, t, h[1]) && is_live(&a, t, h[3]));

    /* Reuse: the slot comes back under a new generation; the old handle stays stale. */
    sc_handle again;
    assert(sc_handle_alloc(&a, t, &again));
    assert(again.index == 2 && again.generation == 1);
    assert(is_live(&a, t, again));
    assert(!is_live(&a, t, h[2]));
    assert(!sc_handle_free(&a, t, h[2]));
    assert(is_live(&a, t, again));

    /* Lowest free slot wins. */
    assert(sc_handle_free(&a, t, h[3]));
    assert(sc_handle_free(&a, t, h[0]));
    sc_handle low;
    assert(sc_handle_alloc(&a, t, &low));
    assert(low.index == 0 && low.generation == 1);

    /* Out of range and wrong generation are none, and free refuses them. */
    const size_t idx[] = {4, 5, SIZE_MAX / 3, SIZE_MAX};
    for (size_t i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        sc_handle bad = {idx[i], 0};
        assert(!is_live(&a, t, bad));
        assert(!sc_handle_free(&a, t, bad));
    }
    sc_handle future = {1, 1};
    assert(!is_live(&a, t, future) && !sc_handle_free(&a, t, future));
    sc_handle huge = {1, UINT32_MAX};
    assert(!is_live(&a, t, huge) && !sc_handle_free(&a, t, huge));
    assert(is_live(&a, t, h[1]));
}

static void test_wrap_retires(void) {
    alignas(8) uint8_t storage[16];
    sc_arena a;
    sc_arena_init(&a, storage, sizeof storage);
    sc_handle_table t = sc_handle_table_new(&a, 2);

    /* Walk slot 0 through every generation; the carry into the high byte at
     * 255 -> 256 is on the way. */
    sc_handle h;
    sc_handle prev = {0, 0};
    for (unsigned g = 0; g <= SC_HANDLE_GEN_MAX; g++) {
        assert(sc_handle_alloc(&a, t, &h));
        assert(h.index == 0 && h.generation == g);
        if (g > 0)
            assert(!is_live(&a, t, prev));
        assert(storage[1] == (g & 0xFF) && storage[2] == (g >> 8));
        if (g < SC_HANDLE_GEN_MAX)
            assert(sc_handle_free(&a, t, h));
        prev = h;
    }

    /* Freeing at the last generation retires the slot rather than wrapping. */
    assert(sc_handle_free(&a, t, h));
    assert(storage[0] == 2 && storage[1] == 0xFF && storage[2] == 0xFF);
    assert(!is_live(&a, t, h));
    assert(!sc_handle_free(&a, t, h));
    sc_handle wrapped = {0, 0};
    assert(!is_live(&a, t, wrapped));

    /* A retired slot is never issued again: slot 1 is next, then the table is full. */
    sc_handle next;
    assert(sc_handle_alloc(&a, t, &next));
    assert(next.index == 1 && next.generation == 0);
    sc_handle out = {5, 5};
    assert(!sc_handle_alloc(&a, t, &out));
    assert(out.index == 5 && out.generation == 5);
    assert(sc_handle_free(&a, t, next));
    assert(sc_handle_alloc(&a, t, &next) && next.index == 1 && next.generation == 1);
}

static void test_stale_table(void) {
    alignas(8) uint8_t storage[32];
    alignas(8) uint8_t other_storage[32];
    sc_arena a, other;
    sc_arena_init(&a, storage, sizeof storage);
    sc_arena_init(&other, other_storage, sizeof other_storage);
    sc_handle_table t = sc_handle_table_new(&a, 3);
    sc_handle h;
    assert(sc_handle_alloc(&a, t, &h));

    /* Resetting a different arena changes nothing. */
    assert(sc_arena_reset(&other));
    assert(is_live(&a, t, h));

    /* After the owner resets, every operation on the old table refuses, and
     * none of them writes to the reused storage. */
    assert(sc_arena_reset(&a));
    for (size_t i = 0; i < sizeof storage; i++)
        storage[i] = 0x5A;
    assert(!is_live(&a, t, h));
    assert(!sc_handle_free(&a, t, h));
    sc_handle out = {42, 42};
    assert(!sc_handle_alloc(&a, t, &out));
    assert(out.index == 42 && out.generation == 42);
    for (size_t i = 0; i < sizeof storage; i++)
        assert(storage[i] == 0x5A);

    /* A new table in the new epoch starts over. */
    sc_handle_table fresh = sc_handle_table_new(&a, 3);
    assert(fresh.cap == 3 && fresh.slots.epoch == a.epoch);
    assert(sc_handle_alloc(&a, fresh, &out) && out.index == 0 && out.generation == 0);
}

int main(void) {
    test_new();
    test_alloc_free_reuse_full();
    test_wrap_retires();
    test_stale_table();
    return 0;
}
