#ifndef SAFEC_ADAPTER_MEM_H
#define SAFEC_ADAPTER_MEM_H

#include "safec/arena.h"
#include "safec/result.h"

/* The memory adapter (adapters/mem.c, subset rule R0): one mmap'd region carved
 * into `count` arenas of equal size, handed out by index (plan KTD5, KTD11).
 * Application code cannot take a pointer into an sc_tslice element, so the
 * adapter keeps the sc_arena structs and returns `sc_arena *`.
 *
 * The pool is not thread-safe. The thread that created it publishes it to the
 * others with a synchronizing operation (thread start), after which they only
 * read its fields; each arena is used by one thread at a time, and moves
 * between threads only through sc_chan or sc_mbox. The fields are private. */
typedef struct {
    uint8_t *base;      /* the mapping */
    size_t len;         /* mapped bytes, a multiple of the runtime page size */
    size_t arena_bytes; /* each arena's capacity, a multiple of 8 */
    size_t count;
    sc_arena *arenas;   /* `count` structs at the end of the mapping */
} sc_mem_pool;

/* Maps one region for `count` arenas of `arena_bytes` each (rounded up to 8),
 * plus their sc_arena structs, rounded up to the runtime page size. Each arena
 * is 8-byte aligned and empty at epoch 0. A zero count or size, or a size that
 * overflows, fails with EINVAL; a failed map gives its errno. On failure *p
 * holds no mapping. */
sc_result_size sc_mem_pool_new(sc_mem_pool *p, size_t count, size_t arena_bytes);

/* The arena at `index`, or NULL for an index at or past `count` or a pool
 * with no mapping. */
sc_arena *sc_mem_arena(const sc_mem_pool *p, size_t index);

/* Resets the arena at `index`, so every slice taken from it goes stale. When
 * the epoch is at UINT_MAX and sc_arena_reset refuses, the arena is
 * initialised again at epoch 0, as adapters/microhttpd.c does: the caller
 * must hold no slice from it past this call. Returns false for a bad index. */
bool sc_mem_reset(sc_mem_pool *p, size_t index);

/* Unmaps the region; every arena and slice from it is gone, and *p holds no
 * mapping. The value is 0; a pool with no mapping fails with EINVAL. */
sc_result_size sc_mem_pool_free(sc_mem_pool *p);

bool sc_mem_erase(const sc_arena *a, sc_slice bytes);

#endif
