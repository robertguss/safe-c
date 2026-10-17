#include "safec/arena.h"
#include "safec/handle.h"
#include "safec/result.h"

#define SLOT_FREE 0
#define SLOT_LIVE 1
#define SLOT_RETIRED 2

/*@ requires valid_arena(a) && slice_storage(a, s);
    assigns \nothing;
    ensures \result <==> valid_slice(a, s);
*/
static bool table_live(const sc_arena *a, sc_slice s) {
    return s.epoch == a->epoch &&
           (s.len == 0 || (s.len <= a->used && s.ptr >= a->base &&
                          s.ptr <= a->base + (a->used - s.len)));
}

/*@ requires t.cap <= SIZE_MAX / 3 && t.slots.len == 3 * t.cap;
    requires t.slots.len == 0 || \valid_read(t.slots.ptr + (0 .. t.slots.len - 1));
    requires i < t.cap;
    assigns \nothing;
    ensures \result == slot_gen(t, i);
*/
static unsigned slot_generation(sc_handle_table t, size_t i) {
    return (unsigned)t.slots.ptr[3 * i + 1] + 256u * (unsigned)t.slots.ptr[3 * i + 2];
}

sc_handle_table sc_handle_table_new(sc_arena *a, size_t cap) {
    sc_handle_table t = {{NULL, 0, a->epoch}, 0};
    if (cap == 0 || cap > SIZE_MAX / 3)
        return t;
    sc_slice s = sc_arena_alloc(a, 3 * cap);
    if (s.len == 0)
        return t;
    /*@ loop invariant 0 <= i <= s.len;
        loop invariant \forall integer k; 0 <= k < i ==> s.ptr[k] == 0;
        loop assigns i, s.ptr[0 .. s.len - 1];
        loop variant s.len - i;
    */
    for (size_t i = 0; i < s.len; i++)
        s.ptr[i] = 0;
    t.slots = s;
    t.cap = cap;
    return t;
}

bool sc_handle_alloc(const sc_arena *a, sc_handle_table t, sc_handle *out) {
    if (!table_live(a, t.slots))
        return false;
    size_t i = 0;
    /*@ loop invariant 0 <= i <= t.cap;
        loop invariant \forall integer k; 0 <= k < i ==> slot_state(t, k) != 0;
        loop assigns i;
        loop variant t.cap - i;
    */
    while (i < t.cap && t.slots.ptr[3 * i] != SLOT_FREE)
        i++;
    if (i == t.cap)
        return false;
    t.slots.ptr[3 * i] = SLOT_LIVE;
    out->index = i;
    out->generation = slot_generation(t, i);
    return true;
}

bool sc_handle_free(const sc_arena *a, sc_handle_table t, sc_handle h) {
    if (!table_live(a, t.slots) || h.index >= t.cap)
        return false;
    size_t at = 3 * h.index;
    if (t.slots.ptr[at] != SLOT_LIVE || slot_generation(t, h.index) != h.generation)
        return false;
    if (h.generation == SC_HANDLE_GEN_MAX) {
        t.slots.ptr[at] = SLOT_RETIRED;
        return true;
    }
    t.slots.ptr[at] = SLOT_FREE;
    if (t.slots.ptr[at + 1] == 255) {
        t.slots.ptr[at + 1] = 0;
        t.slots.ptr[at + 2] = (uint8_t)(t.slots.ptr[at + 2] + 1);
    } else {
        t.slots.ptr[at + 1] = (uint8_t)(t.slots.ptr[at + 1] + 1);
    }
    return true;
}

sc_option_size sc_handle_get(const sc_arena *a, sc_handle_table t, sc_handle h) {
    if (!table_live(a, t.slots) || h.index >= t.cap)
        return sc_none_size();
    if (t.slots.ptr[3 * h.index] != SLOT_LIVE || slot_generation(t, h.index) != h.generation)
        return sc_none_size();
    return sc_some_size(h.index);
}
