#ifndef SAFEC_TESTS_SIDE_H
#define SAFEC_TESTS_SIDE_H

#include "safec/arena.h"
#include "safec/handle.h"
#include <assert.h>
#include <stdalign.h>
#include <stdint.h>

/* One thread's half of a channel or mailbox test: an arena and a handle table.
 * Define SIDE_STORE before including this to change the arena size. */
#ifndef SIDE_STORE
#define SIDE_STORE 256
#endif

typedef struct {
    alignas(8) uint8_t store[SIDE_STORE];
    sc_arena arena;
    sc_handle_table table;
} side;

static inline void side_init(side *s, size_t slots) {
    sc_arena_init(&s->arena, s->store, sizeof s->store);
    s->table = sc_handle_table_new(&s->arena, slots);
    assert(s->table.cap == slots);
}

static inline sc_handle take(side *s) {
    sc_handle h;
    assert(sc_handle_alloc(&s->arena, s->table, &h));
    return h;
}

static inline bool live(side *s, sc_handle h) {
    return sc_handle_get(&s->arena, s->table, h).some;
}

#endif
