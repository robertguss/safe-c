/* Fuzzes the memory adapter. Input bytes choose a pool shape and a run of
 * index lookups, allocations and resets. Every arena handed out must lie inside
 * the mapping and inside its own stride, every slice inside its arena, and a
 * reset must make the arena's earlier slices stale. A reference model counts
 * each arena's used bytes. */
#include "../adapters/mem.c"
#include <assert.h>

#define MAX_COUNT 16

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 3)
        return 0;
    size_t count = 1 + data[0] % MAX_COUNT;
    size_t bytes = 1 + (((size_t)data[1] << 8) | data[2]);
    sc_mem_pool p;
    if (!sc_mem_pool_new(&p, count, bytes).ok)
        return 0;
    size_t each = (bytes + 7) / 8 * 8;
    assert(p.count == count && p.arena_bytes == each);
    size_t used[MAX_COUNT] = {0};
    sc_slice last[MAX_COUNT] = {{0}};

    for (size_t k = 3; k + 1 < size; k += 2) {
        size_t index = data[k] % (count + 2); /* reaches count and count + 1 */
        uint8_t op = data[k + 1];
        sc_arena *a = sc_mem_arena(&p, index);
        if (index >= count) {
            assert(a == NULL && !sc_mem_reset(&p, index));
            continue;
        }
        assert(a != NULL);
        assert((uint8_t *)a >= p.base + count * each && (uint8_t *)(a + 1) <= p.base + p.len);
        assert(a->base == p.base + index * each && a->cap == each);
        if (op & 1) {
            sc_slice before = last[index];
            assert(sc_mem_reset(&p, index));
            used[index] = 0;
            uint8_t out = 0;
            if (before.len > 0)
                assert(!sc_slice_get(a, before, 0, &out));
            last[index] = (sc_slice){0};
        } else {
            size_t n = (size_t)(op >> 1) * 37;
            sc_slice s = sc_arena_alloc(a, n);
            size_t rounded = (n + 7) / 8 * 8;
            if (n > 0 && rounded <= each - used[index]) {
                assert(s.ptr == a->base + used[index] && s.len == n);
                assert(s.ptr + s.len <= a->base + a->cap);
                s.ptr[0] = 0xAA;
                s.ptr[n - 1] = 0x55;
                used[index] += rounded;
                last[index] = s;
            } else {
                assert(s.ptr == NULL && s.len == 0);
            }
            assert(a->used == used[index]);
        }
    }
    /* End on the stale path: reset every arena, and each one's last slice is refused. */
    for (size_t i = 0; i < count; i++) {
        assert(sc_mem_reset(&p, i));
        uint8_t out = 0;
        if (last[i].len > 0)
            assert(!sc_slice_get(sc_mem_arena(&p, i), last[i], 0, &out));
    }
    assert(sc_mem_pool_free(&p).ok);
    return 0;
}
