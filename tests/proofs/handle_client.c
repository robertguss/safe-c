#include "safec/handle.h"

/* The caller checks the tag before it unwraps; the index is only used for a
 * live handle. */
/*@ requires valid_arena(a) && table_storage(a, t);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures handle_live(a, t, h) ==> \result == h.index;
    ensures !handle_live(a, t, h) ==> \result == t.cap;
*/
size_t checked_handle_client(const sc_arena *a, sc_handle_table t, sc_handle h) {
    sc_option_size o = sc_handle_get(a, t, h);
    if (!sc_option_size_is_some(o))
        return t.cap;
    return sc_option_size_unwrap(o);
}

/* A freed handle returns none on the next get, whatever state it was in. */
/*@ requires valid_arena(a) && table_storage(a, t);
    terminates \true;
    exits \false;
    assigns t.slots[3 * h.index .. 3 * h.index + 2];
    ensures \result == 0;
*/
int freed_handle_client(const sc_arena *a, sc_handle_table t, sc_handle h) {
    (void)sc_handle_free(a, t, h);
    sc_option_size o = sc_handle_get(a, t, h);
    return sc_option_size_is_some(o) ? 1 : 0;
}
