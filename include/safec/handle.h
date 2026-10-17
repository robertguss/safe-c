#ifndef SAFEC_HANDLE_H
#define SAFEC_HANDLE_H

#include "arena.h"
#include "result.h"

/* A fixed-capacity table of slots carved from one arena allocation. Each slot
 * is three bytes: a state (0 free, 1 live, 2 retired) and a 16-bit generation,
 * low byte first. Bytes keep the table inside WP's typed memory model, which
 * rejects a cast from arena bytes to a wider type. The table is a value: its
 * fields never change after sc_handle_table_new, and the slot bytes live in
 * the arena, so it carries the arena's epoch like a slice does.
 * The fields are private. The slot bytes are deliberately not an sc_slice, so
 * the slice API cannot reach them; code that writes them by other means can
 * forge a live handle, and no check here can see it. */
typedef struct {
    uint8_t *slots;
    size_t cap;
    unsigned epoch;
} sc_handle_table;

/* A handle names a slot, the generation it was issued under, and the table it
 * came from: the arena epoch and the table's byte offset in the arena. Two
 * live tables in one arena never share an offset, and a reset changes the
 * epoch, so a handle is refused by every table but its own. The caller passes
 * each table with the arena it came from; a handle carried to a table in a
 * different arena is refused only if the epoch or offset differs. */
typedef struct {
    size_t index;
    unsigned generation;
    unsigned epoch;
    size_t table;
} sc_handle;

/* The last generation a slot can carry. Freeing a slot at this generation
 * retires it for the rest of the table's lifetime instead of wrapping to 0,
 * so a stale handle can never match again. */
#define SC_HANDLE_GEN_MAX 65535u

/*@ logic integer slot_state(sc_handle_table t, integer i) = t.slots[3 * i];
    logic integer slot_gen(sc_handle_table t, integer i) =
      t.slots[3 * i + 1] + 256 * t.slots[3 * i + 2];
    predicate table_storage(sc_arena *a, sc_handle_table t) =
      t.cap <= SIZE_MAX / 3 &&
      (t.cap == 0 ||
       (\base_addr(t.slots) == \base_addr(a->base) &&
        \valid(t.slots + (0 .. 3 * t.cap - 1)) &&
        t.slots >= a->base && t.slots + 3 * t.cap <= a->base + a->cap));
    predicate valid_table(sc_arena *a, sc_handle_table t) =
      table_storage(a, t) && t.epoch == a->epoch &&
      (t.cap == 0 ||
       (3 * t.cap <= a->used && t.slots <= a->base + (a->used - 3 * t.cap)));
    predicate handle_live(sc_arena *a, sc_handle_table t, sc_handle h) =
      valid_table(a, t) && h.index < t.cap &&
      h.epoch == t.epoch && h.table == t.slots - a->base &&
      slot_state(t, h.index) == 1 && slot_gen(t, h.index) == h.generation;
*/

/* Allocates 3 * cap bytes and marks every slot free at generation 0. A zero
 * capacity, an oversized one, or a full arena returns the empty table
 * (no slots) and leaves the arena untouched. */
/*@ requires valid_arena(a) && \valid(a);
    terminates \true;
    exits \false;
    assigns a->used, a->base[a->used .. a->cap - 1];
    ensures valid_arena(a);
    ensures \result.epoch == a->epoch;
    ensures table_storage(a, \result);
    behavior fits:
      assumes cap > 0 && cap <= SIZE_MAX / 3 && 3 * cap <= a->cap - a->used &&
              3 * cap <= SIZE_MAX - 7 && (3 * cap + 7) / 8 * 8 <= a->cap - a->used;
      assigns a->used, a->base[a->used .. a->used + 3 * cap - 1];
      ensures \result.cap == cap;
      ensures \result.slots == a->base + \old(a->used);
      ensures a->used == \old(a->used) + (3 * cap + 7) / 8 * 8;
      ensures valid_table(a, \result);
      ensures \forall integer i; 0 <= i < cap ==>
                slot_state(\result, i) == 0 && slot_gen(\result, i) == 0;
    behavior empty:
      assumes !(cap > 0 && cap <= SIZE_MAX / 3 && 3 * cap <= a->cap - a->used &&
                3 * cap <= SIZE_MAX - 7 && (3 * cap + 7) / 8 * 8 <= a->cap - a->used);
      ensures \result.cap == 0 && \result.slots == \null;
      ensures a->used == \old(a->used);
    complete behaviors;
    disjoint behaviors;
*/
sc_handle_table sc_handle_table_new(sc_arena *a, size_t cap);

/* Issues a handle to the lowest free slot and marks it live. A stale table or
 * a table with no free slot returns false and changes nothing. Retired slots
 * are never issued again. */
/*@ requires valid_arena(a) && table_storage(a, t);
    requires \valid(out);
    requires \separated(out, a, a->base + (0 .. a->cap - 1));
    terminates \true;
    exits \false;
    assigns *out, t.slots[0 .. 3 * t.cap - 1];
    behavior unavailable:
      assumes !valid_table(a, t) ||
              (\forall integer i; 0 <= i < t.cap ==> slot_state(t, i) != 0);
      assigns \nothing;
      ensures !\result;
    behavior issued:
      assumes valid_table(a, t) &&
              (\exists integer i; 0 <= i < t.cap && slot_state(t, i) == 0);
      ensures \result;
      ensures out->index < t.cap;
      ensures \forall integer i; i == out->index ==>
                \at(slot_state(t, i), Pre) == 0 && out->generation == \at(slot_gen(t, i), Pre);
      ensures \forall integer i; 0 <= i < out->index ==> \at(slot_state(t, i), Pre) != 0;
      ensures handle_live(a, t, *out);
      ensures \forall integer k; 0 <= k < 3 * t.cap && k != 3 * out->index ==>
                t.slots[k] == \old(t.slots[k]);
    complete behaviors;
    disjoint behaviors;
*/
bool sc_handle_alloc(const sc_arena *a, sc_handle_table t, sc_handle *out);

/* Frees the slot a live handle names: the generation moves on, so this handle
 * and every copy of it go stale. A slot at SC_HANDLE_GEN_MAX retires instead.
 * A stale, foreign, out-of-range or already-freed handle returns false and
 * changes nothing. */
/*@ requires valid_arena(a) && table_storage(a, t);
    terminates \true;
    exits \false;
    assigns t.slots[3 * h.index .. 3 * h.index + 2];
    ensures \result <==> \old(handle_live(a, t, h));
    ensures !handle_live(a, t, h);
    behavior dead:
      assumes !handle_live(a, t, h);
      assigns \nothing;
    behavior reuse:
      assumes handle_live(a, t, h) && h.generation < 65535;
      ensures slot_state(t, h.index) == 0;
      ensures slot_gen(t, h.index) == h.generation + 1;
    behavior retire:
      assumes handle_live(a, t, h) && h.generation >= 65535;
      ensures slot_state(t, h.index) == 2;
      ensures slot_gen(t, h.index) == h.generation;
    complete behaviors;
    disjoint behaviors;
*/
bool sc_handle_free(const sc_arena *a, sc_handle_table t, sc_handle h);

/* Returns some(slot index) for a live handle, so the caller can index its own
 * per-slot storage, and none for a stale table, a handle from another table
 * or epoch, an out-of-range index, a freed or retired slot, or an old
 * generation. */
/*@ requires valid_arena(a) && table_storage(a, t);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures (\result.some != 0) <==> handle_live(a, t, h);
    ensures \result.some != 0 ==> \result.value == h.index;
    ensures \result.some == 0 ==> \result.value == 0;
*/
sc_option_size sc_handle_get(const sc_arena *a, sc_handle_table t, sc_handle h);

#endif
