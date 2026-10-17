#include "safec/arena.h"

/*@ requires valid_arena(a) && \valid(a);
    requires a->used == 0 && a->cap >= 8;
    assigns a->used;
    ensures \result.ptr == a->base && \result.len == 1 && a->used == 8;
*/
sc_slice allocation_client(sc_arena *a) {
    return sc_arena_alloc(a, 1);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && s.len > 0;
    requires \valid(out);
    requires \separated(out, a, a->base + (0 .. a->cap - 1));
    assigns *out;
    ensures \result && *out == s.ptr[0];
*/
bool read_client(const sc_arena *a, sc_slice s, uint8_t *out) {
    return sc_slice_get(a, s, 0, out);
}

/*@ requires valid_arena(a) && \valid(a);
    requires a->used == 0 && a->cap >= 8 && a->epoch < UINT_MAX;
    requires \valid(out);
    requires \separated(out, a, a->base + (0 .. a->cap - 1));
    assigns a->used, a->epoch, *out;
    ensures !\result && *out == \old(*out);
    ensures a->used == 8 && a->epoch == \old(a->epoch) + 1;
*/
bool stale_after_reuse_client(sc_arena *a, uint8_t *out) {
    sc_slice stale = sc_arena_alloc(a, 1);
    (void)sc_arena_reset(a);
    (void)sc_arena_alloc(a, 1);
    return sc_slice_get(a, stale, 0, out);
}
