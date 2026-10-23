#include "safec/handle.h"

/*@ predicate prop_table_context(sc_arena *a, sc_handle_table t) =
      valid_arena(a) && table_storage(a, t);

    predicate prop_alloc_context(sc_arena *a, sc_handle_table t,
                                 sc_handle *out) =
      prop_table_context(a, t) &&
      \valid(out) && \separated(out, a) &&
      \separated(out, a->base + (0 .. a->cap - 1));

    predicate prop_new_room(sc_arena *a, integer cap) =
      valid_arena(a) && \valid(a) &&
      0 < cap <= (SIZE_MAX - 7) / 3 &&
      ((3 * cap + 7) / 8) * 8 <= a->cap - a->used;
*/

/*@ requires prop_new_room(a, cap);
    terminates \true;
    exits \false;
    assigns a->used, a->base[a->used .. a->used + 3 * cap - 1];
    ensures \result.cap == cap;
    ensures valid_table(a, \result);
    ensures \result.epoch == \old(a->epoch);
    ensures a->used >= \old(a->used) + 3 * cap;
    ensures \result.slots >= a->base + \old(a->used);
    ensures \forall integer i; 0 <= i < cap ==>
      slot_state(\result, i) == 0;
*/
sc_handle_table prop_new_initializes_free_table(sc_arena *a, size_t cap)
{
    return sc_handle_table_new(a, cap);
}

/*@ requires prop_alloc_context(a, t, out);
    terminates \true;
    exits \false;
    assigns *out, t.slots[0 .. 3 * t.cap - 1];
    ensures \result <==>
      (\old(valid_table(a, t)) &&
       (\exists integer i; 0 <= i < t.cap &&
         \old(slot_state(t, i)) == 0));
    ensures \result ==> handle_live(a, t, *out);
    ensures \result ==> \old(slot_state(t, out->index)) == 0;
    ensures \result ==> (\forall integer i; 0 <= i < out->index ==>
      \old(slot_state(t, i)) != 0);
    ensures \result ==> (\forall integer i; 0 <= i < t.cap ==>
      slot_gen(t, i) == \old(slot_gen(t, i)));
    ensures \result ==> (\forall integer i;
      0 <= i < t.cap && i != out->index ==>
        slot_state(t, i) == \old(slot_state(t, i)));
    ensures !\result ==>
      out->index == \old(out->index) &&
      out->generation == \old(out->generation) &&
      out->epoch == \old(out->epoch) &&
      out->table == \old(out->table);
    ensures !\result ==> (\forall integer j; 0 <= j < 3 * t.cap ==>
      t.slots[j] == \old(t.slots[j]));
*/
bool prop_alloc_lowest_free_slot(sc_arena *a, sc_handle_table t,
                                sc_handle *out)
{
    return sc_handle_alloc(a, t, out);
}

/*@ requires prop_table_context(a, t);
    terminates \true;
    exits \false;
    assigns t.slots[0 .. 3 * t.cap - 1];
    ensures \result <==> \old(handle_live(a, t, h));
    ensures !handle_live(a, t, h);
    ensures \result && h.generation < SC_HANDLE_GEN_MAX ==>
      slot_state(t, h.index) == 0 &&
      slot_gen(t, h.index) == h.generation + 1;
    ensures \result && h.generation == SC_HANDLE_GEN_MAX ==>
      slot_state(t, h.index) != 0 &&
      slot_state(t, h.index) != 1 &&
      slot_gen(t, h.index) == SC_HANDLE_GEN_MAX;
    ensures \result ==> (\forall integer i;
      0 <= i < t.cap && i != h.index ==>
        slot_state(t, i) == \old(slot_state(t, i)) &&
        slot_gen(t, i) == \old(slot_gen(t, i)));
    ensures !\result ==> (\forall integer j; 0 <= j < 3 * t.cap ==>
      t.slots[j] == \old(t.slots[j]));
*/
bool prop_free_invalidates_and_retires(sc_arena *a, sc_handle_table t,
                                      sc_handle h)
{
    return sc_handle_free(a, t, h);
}

/*@ requires prop_table_context(a, t);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result.some <==> handle_live(a, t, h);
    ensures \result.some ==> \result.value == h.index;
*/
sc_option_size prop_get_exact_live_index(sc_arena *a, sc_handle_table t,
                                        sc_handle h)
{
    return sc_handle_get(a, t, h);
}
