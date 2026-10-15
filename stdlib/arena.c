#include "safec/arena.h"

void sc_arena_init(sc_arena *a, uint8_t *buf, size_t cap) {
    a->base = buf;
    a->cap = cap;
    a->used = 0;
    a->epoch = 0;
}

bool sc_arena_is_valid(const sc_arena *a) {
    return a != NULL && a->used <= a->cap && a->used % 8 == 0 &&
           a->cap <= PTRDIFF_MAX && (a->cap == 0 || a->base != NULL);
}

sc_slice sc_arena_alloc(sc_arena *a, size_t n) {
    sc_slice s = {NULL, 0, a->epoch};
    if (n == 0 || n > a->cap - a->used || n > SIZE_MAX - 7)
        return s;
    size_t rounded = (n + 7) / 8 * 8;
    if (rounded > a->cap - a->used)
        return s;
    s.ptr = a->base + a->used;
    s.len = n;
    a->used += rounded;
    return s;
}

bool sc_arena_reset(sc_arena *a) {
    if (a->epoch == UINT_MAX)
        return false;
    a->used = 0;
    a->epoch++;
    return true;
}
