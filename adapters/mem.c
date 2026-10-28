/* Memory adapter (subset rule R0). It maps the explorer's arena pool with mmap,
 * which application code cannot call, and carves it with pointer arithmetic,
 * which application code cannot do. */
#define _DEFAULT_SOURCE
#include "sc_mem.h"
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

/* `n` rounded up to a multiple of `unit`, or 0 if that overflows. */
static size_t mem_round_up(size_t n, size_t unit) {
    size_t r = n % unit;
    if (r == 0)
        return n;
    return n > SIZE_MAX - (unit - r) ? 0 : n + (unit - r);
}

/* The mapping length for `count` arenas of `each` bytes (a multiple of 8) and
 * their structs, rounded to `page`; 0 on overflow. */
static size_t mem_layout(size_t count, size_t each, size_t page) {
    if (each == 0 || count > SIZE_MAX / each)
        return 0;
    size_t storage = count * each;
    if (count > (SIZE_MAX - storage) / sizeof(sc_arena))
        return 0;
    return mem_round_up(storage + count * sizeof(sc_arena), page);
}

static void mem_empty(sc_mem_pool *p) {
    p->base = NULL;
    p->len = 0;
    p->arena_bytes = 0;
    p->count = 0;
    p->arenas = NULL;
}

sc_result_size sc_mem_pool_new(sc_mem_pool *p, size_t count, size_t arena_bytes) {
    mem_empty(p);
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        return sc_err_size(errno != 0 ? errno : EINVAL);
    if (count == 0 || arena_bytes == 0)
        return sc_err_size(EINVAL);
    size_t each = mem_round_up(arena_bytes, 8);
    size_t len = each == 0 ? 0 : mem_layout(count, each, (size_t)page);
    if (len == 0)
        return sc_err_size(EINVAL);
    void *m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED)
        return sc_err_size(errno);
    p->base = m;
    p->len = len;
    p->arena_bytes = each;
    p->count = count;
    /* The structs follow the storage; `each` is a multiple of 8, so they are aligned. */
    p->arenas = (sc_arena *)(void *)(p->base + count * each);
    for (size_t i = 0; i < count; i++)
        sc_arena_init(&p->arenas[i], p->base + i * each, each);
    return sc_ok_size(0);
}

sc_arena *sc_mem_arena(const sc_mem_pool *p, size_t index) {
    if (p->base == NULL || index >= p->count)
        return NULL;
    return &p->arenas[index];
}

bool sc_mem_reset(sc_mem_pool *p, size_t index) {
    sc_arena *a = sc_mem_arena(p, index);
    if (a == NULL)
        return false;
    if (!sc_arena_reset(a))
        sc_arena_init(a, p->base + index * p->arena_bytes, p->arena_bytes);
    return true;
}

sc_result_size sc_mem_pool_free(sc_mem_pool *p) {
    if (p->base == NULL)
        return sc_err_size(EINVAL);
    int rc = munmap(p->base, p->len);
    mem_empty(p);
    if (rc != 0)
        return sc_err_size(errno);
    return sc_ok_size(0);
}

bool sc_mem_erase(const sc_arena *a, sc_slice bytes) {
    if (bytes.epoch != a->epoch)
        return false;
    sc_slice live = sc_slice_sub(a, bytes, 0, bytes.len);
    if (live.len != bytes.len)
        return false;
    volatile uint8_t *p = live.ptr;
    for (size_t i = 0; i < live.len; i++)
        p[i] = 0;
    return true;
}
