#ifndef SAFEC_ARENA_H
#define SAFEC_ARENA_H

#include "slice.h"
#include <limits.h>

/* The caller supplies an 8-byte-aligned backing buffer, valid for cap bytes.
 * Init is for a new lifetime: do not reinitialize an arena with outstanding
 * slices. Reset preserves the epoch history. No function allocates storage. */
/*@ requires \valid(a);
    requires cap <= PTRDIFF_MAX;
    requires cap > 0 ==> \valid(buf + (0 .. cap - 1)) && \aligned(buf, 8);
    requires \separated(a, buf + (0 .. cap - 1));
    terminates \true;
    exits \false;
    assigns *a;
    ensures valid_arena(a);
    ensures base_alignment: cap > 0 ==> \aligned(a->base, 8);
    ensures a->base == buf && a->cap == cap && a->used == 0 && a->epoch == 0;
*/
void sc_arena_init(sc_arena *a, uint8_t *buf, size_t cap);

/* This checks fields only; portable C cannot establish pointer validity,
 * backing storage lifetime, or the caller's base-alignment obligation. */
/*@ requires a == \null || \valid_read(a);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures \result <==> a != \null && arena_shape(a) && a->cap <= PTRDIFF_MAX;
*/
bool sc_arena_is_valid(const sc_arena *a);

/* Allocation rounds the cursor up to 8 bytes; zero and failure return empty. */
/*@ requires valid_arena(a) && \valid(a);
    requires base_alignment: a->cap > 0 ==> \aligned(a->base, 8);
    terminates \true;
    exits \false;
    assigns a->used;
    ensures valid_arena(a);
    ensures \result.epoch == a->epoch;
    behavior fits:
      assumes n > 0 && n <= a->cap - a->used && n <= SIZE_MAX - 7 &&
              (n + 7) / 8 * 8 <= a->cap - a->used;
      ensures \result.ptr == a->base + \old(a->used) && \result.len == n;
      ensures a->used == \old(a->used) + (n + 7) / 8 * 8;
      ensures aligned_result: \aligned(\result.ptr, 8);
      ensures valid_slice(a, \result);
      ensures \separated(\result.ptr + (0 .. n - 1), a->base + (0 .. \old(a->used) - 1));
    behavior fails:
      assumes !(n > 0 && n <= a->cap - a->used && n <= SIZE_MAX - 7 &&
                (n + 7) / 8 * 8 <= a->cap - a->used);
      ensures \result.ptr == \null && \result.len == 0;
      ensures a->used == \old(a->used);
    complete behaviors;
    disjoint behaviors;
*/
sc_slice sc_arena_alloc(sc_arena *a, size_t n);

/* Refuse to wrap: stale slices must never become current again. */
/*@ requires valid_arena(a) && \valid(a);
    terminates \true;
    exits \false;
    assigns a->used, a->epoch;
    ensures valid_arena(a);
    ensures \result <==> \old(a->epoch) < UINT_MAX;
    ensures \result ==> a->used == 0 && a->epoch == \old(a->epoch) + 1;
    ensures !\result ==> a->used == \old(a->used) && a->epoch == \old(a->epoch);
*/
bool sc_arena_reset(sc_arena *a);

#endif
