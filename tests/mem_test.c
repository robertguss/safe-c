/* Tests the memory adapter (adapters/mem.c). Adapters are not in the library,
 * so the test builds the adapter in directly. */
#include "../adapters/mem.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define KIB 1024u

static void expect_err(sc_result_size r, int err) {
    assert(!r.ok && r.err == err && r.value == 0);
}

static size_t page_size(void) {
    long page = sysconf(_SC_PAGESIZE);
    assert(page > 0);
    return (size_t)page;
}

/* N = 8 arenas of 256 KiB: distinct, non-overlapping, 8-byte aligned, each
 * usable to its last byte without touching a neighbour. */
static void test_carve(void) {
    sc_mem_pool p;
    sc_result_size r = sc_mem_pool_new(&p, 8, 256 * KIB);
    assert(r.ok && r.value == 0);
    assert(p.count == 8 && p.arena_bytes == 256 * KIB);
    assert(p.len % page_size() == 0 && p.len >= 8 * 256 * KIB + 8 * sizeof(sc_arena));
    for (size_t i = 0; i < 8; i++) {
        sc_arena *a = sc_mem_arena(&p, i);
        assert(a != NULL && sc_arena_is_valid(a));
        assert(a->cap == 256 * KIB && a->used == 0 && a->epoch == 0);
        assert((uintptr_t)a->base % 8 == 0);
        assert(a->base >= p.base && a->base + a->cap <= p.base + p.len);
        /* The struct sits in the mapping too, outside every arena's bytes. */
        assert((uint8_t *)a >= p.base + 8 * 256 * KIB && (uint8_t *)(a + 1) <= p.base + p.len);
        for (size_t j = 0; j < i; j++) {
            sc_arena *b = sc_mem_arena(&p, j);
            assert(a != b);
            assert(a->base + a->cap <= b->base || b->base + b->cap <= a->base);
        }
        sc_slice s = sc_arena_alloc(a, 256 * KIB);
        assert(s.ptr == a->base && s.len == 256 * KIB);
        memset(s.ptr, (int)i + 1, s.len);
    }
    for (size_t i = 0; i < 8; i++) {
        sc_arena *a = sc_mem_arena(&p, i);
        assert(a->base[0] == i + 1 && a->base[a->cap - 1] == i + 1);
    }
    assert(sc_mem_arena(&p, 8) == NULL);
    assert(sc_mem_arena(&p, SIZE_MAX) == NULL);
    assert(sc_mem_pool_free(&p).ok);
    assert(p.base == NULL && p.count == 0 && sc_mem_arena(&p, 0) == NULL);
    expect_err(sc_mem_pool_free(&p), EINVAL);
}

/* Lengths round to the runtime page size, not to 4096. */
static void test_rounding(void) {
    assert(mem_round_up(5000, 16384) == 16384);
    assert(mem_round_up(16384, 16384) == 16384);
    assert(mem_round_up(16385, 16384) == 32768);
    assert(mem_round_up(0, 8) == 0 && mem_round_up(1, 8) == 8);
    assert(mem_round_up(SIZE_MAX, 8) == 0);
    /* One 1000-byte arena and its struct need one page of either size. */
    assert(mem_layout(1, 1000, 16384) == 16384);
    assert(mem_layout(1, 1000, 4096) == 4096);
    assert(mem_layout(2, 8192, 16384) == 32768);
    assert(mem_layout(SIZE_MAX / 8, 16, 4096) == 0);
    assert(mem_layout(SIZE_MAX / 16, 16, 4096) == 0); /* storage fits, structs do not */

    sc_mem_pool p;
    assert(sc_mem_pool_new(&p, 1, 1000).ok);
    assert(p.len == page_size());
    assert(p.arena_bytes == 1000 && sc_mem_arena(&p, 0)->cap == 1000);
    assert(sc_mem_pool_free(&p).ok);
    /* A size that is not a multiple of 8 rounds up, so the next arena stays aligned. */
    assert(sc_mem_pool_new(&p, 3, 13).ok);
    assert(p.arena_bytes == 16);
    assert(sc_mem_arena(&p, 1)->base == sc_mem_arena(&p, 0)->base + 16);
    assert(sc_mem_pool_free(&p).ok);
}

static void test_bad_sizes(void) {
    sc_mem_pool p;
    expect_err(sc_mem_pool_new(&p, 0, 256 * KIB), EINVAL);
    assert(p.base == NULL && p.count == 0);
    expect_err(sc_mem_pool_new(&p, 8, 0), EINVAL);
    expect_err(sc_mem_pool_new(&p, 1, SIZE_MAX), EINVAL);
    expect_err(sc_mem_pool_new(&p, SIZE_MAX, 8), EINVAL);
    /* Fits size_t but not the address space: mmap's own error comes back. */
    sc_result_size r = sc_mem_pool_new(&p, 1, SIZE_MAX / 2);
    assert(!r.ok && r.err == ENOMEM && p.base == NULL);
}

/* Reset makes old slices stale; at UINT_MAX it re-inits instead of failing. */
static void test_reset(void) {
    sc_mem_pool p;
    assert(sc_mem_pool_new(&p, 2, 64).ok);
    sc_arena *a = sc_mem_arena(&p, 0);
    sc_slice s = sc_arena_alloc(a, 16);
    assert(s.ptr != NULL);
    assert(sc_mem_reset(&p, 0));
    assert(a->epoch == 1 && a->used == 0);
    uint8_t out = 7;
    assert(!sc_slice_get(a, s, 0, &out) && out == 7);

    /* The other arena is untouched by a neighbour's reset. */
    sc_arena *b = sc_mem_arena(&p, 1);
    sc_slice t = sc_arena_alloc(b, 8);
    assert(sc_mem_reset(&p, 0));
    assert(sc_slice_get(b, t, 0, &out));

    a->epoch = UINT_MAX;
    a->used = 32;
    assert(sc_mem_reset(&p, 0));
    assert(a->epoch == 0 && a->used == 0 && a->cap == 64 && a->base == p.base);
    assert(!sc_mem_reset(&p, 2));
    assert(sc_mem_pool_free(&p).ok);
    assert(!sc_mem_reset(&p, 0));
}

int main(void) {
    test_carve();
    test_rounding();
    test_bad_sizes();
    test_reset();
    puts("mem_test: ok");
    return 0;
}
