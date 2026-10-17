#include "safec/arena.h"
#include "safec/handle.h"
#include "safec/result.h"

#define SLOT_FREE 0
#define SLOT_LIVE 1
#define SLOT_RETIRED 2

/*@ requires valid_arena(a) && table_storage(a, t);
    assigns \nothing;
    ensures \result <==> valid_table(a, t);
*/
static bool table_live(const sc_arena *a, sc_handle_table t) {
    return t.epoch == a->epoch &&
           (t.cap == 0 || (3 * t.cap <= a->used && t.slots >= a->base &&
                          t.slots <= a->base + (a->used - 3 * t.cap)));
}

/*@ requires t.cap <= SIZE_MAX / 3 && i < t.cap;
    requires \valid_read(t.slots + (0 .. 3 * t.cap - 1));
    assigns \nothing;
    ensures \result == slot_gen(t, i);
*/
static unsigned slot_generation(sc_handle_table t, size_t i) {
    return (unsigned)t.slots[3 * i + 1] + 256u * (unsigned)t.slots[3 * i + 2];
}

/*@ requires valid_arena(a) && table_storage(a, t) && valid_table(a, t);
    assigns \nothing;
    ensures \result <==> handle_live(a, t, h);
*/
static bool handle_matches(const sc_arena *a, sc_handle_table t, sc_handle h) {
    if (h.index >= t.cap || h.epoch != t.epoch)
        return false;
    if (h.table != (size_t)(t.slots - a->base))
        return false;
    return t.slots[3 * h.index] == SLOT_LIVE && slot_generation(t, h.index) == h.generation;
}

sc_handle_table sc_handle_table_new(sc_arena *a, size_t cap) {
    sc_handle_table t = {NULL, 0, a->epoch};
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
    t.slots = s.ptr;
    t.cap = cap;
    return t;
}

bool sc_handle_alloc(const sc_arena *a, sc_handle_table t, sc_handle *out) {
    if (!table_live(a, t))
        return false;
    size_t i = 0;
    /*@ loop invariant 0 <= i <= t.cap;
        loop invariant \forall integer k; 0 <= k < i ==> slot_state(t, k) != 0;
        loop assigns i;
        loop variant t.cap - i;
    */
    while (i < t.cap && t.slots[3 * i] != SLOT_FREE)
        i++;
    if (i == t.cap)
        return false;
    t.slots[3 * i] = SLOT_LIVE;
    out->index = i;
    out->generation = slot_generation(t, i);
    out->epoch = t.epoch;
    out->table = (size_t)(t.slots - a->base);
    return true;
}

bool sc_handle_free(const sc_arena *a, sc_handle_table t, sc_handle h) {
    if (!table_live(a, t) || !handle_matches(a, t, h))
        return false;
    size_t at = 3 * h.index;
    if (h.generation == SC_HANDLE_GEN_MAX) {
        t.slots[at] = SLOT_RETIRED;
        return true;
    }
    t.slots[at] = SLOT_FREE;
    if (t.slots[at + 1] == 255) {
        t.slots[at + 1] = 0;
        t.slots[at + 2] = (uint8_t)(t.slots[at + 2] + 1);
    } else {
        t.slots[at + 1] = (uint8_t)(t.slots[at + 1] + 1);
    }
    return true;
}

sc_option_size sc_handle_get(const sc_arena *a, sc_handle_table t, sc_handle h) {
    if (!table_live(a, t) || !handle_matches(a, t, h))
        return sc_none_size();
    return sc_some_size(h.index);
}
