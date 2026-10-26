#ifndef SAFEC_TSLICE_H
#define SAFEC_TSLICE_H

#include "arena.h"

/* A checked view of an array of equal-sized elements (integers, structs) in an
 * arena. `bytes` is an ordinary sc_slice, so liveness is the sc_slice check;
 * `elem` is the element size in bytes. The element count is bytes.len / elem.
 * Elements are copied in and out; no pointer into the array is handed out.
 *
 * The arena is 8-byte aligned and the stride is sizeof(T), so every element is
 * aligned for any T with _Alignof(T) <= 8. The SC_TSLICE_* macros below
 * enforce that bound and the element type; call the functions directly only
 * for raw bytes. */
typedef struct {
    sc_slice bytes;
    size_t elem;
} sc_tslice;

/* Zero count, zero elem, overflow and exhaustion return the canonical empty
 * value {{NULL, 0, epoch}, 0}. */
/*@ requires valid_arena(a) && \valid(a);
    terminates \true;
    exits \false;
    assigns a->used;
    ensures valid_arena(a);
    ensures \result.bytes.epoch == a->epoch;
    behavior fits:
      assumes elem > 0 && count > 0 && count <= SIZE_MAX / elem &&
              count * elem <= a->cap - a->used && count * elem <= SIZE_MAX - 7 &&
              (count * elem + 7) / 8 * 8 <= a->cap - a->used;
      ensures \result.elem == elem && \result.bytes.len == count * elem;
      ensures \result.bytes.ptr == a->base + \old(a->used);
      ensures valid_slice(a, \result.bytes);
    behavior fails:
      assumes !(elem > 0 && count > 0 && count <= SIZE_MAX / elem &&
                count * elem <= a->cap - a->used && count * elem <= SIZE_MAX - 7 &&
                (count * elem + 7) / 8 * 8 <= a->cap - a->used);
      ensures \result.bytes.ptr == \null && \result.bytes.len == 0 && \result.elem == 0;
      ensures a->used == \old(a->used);
    complete behaviors;
    disjoint behaviors;
*/
sc_tslice sc_tslice_alloc(sc_arena *a, size_t count, size_t elem);

/*@ terminates \true;
    exits \false;
    assigns \nothing;
    ensures s.elem == 0 ==> \result == 0;
    ensures s.elem > 0 ==> \result == s.bytes.len / s.elem;
*/
size_t sc_tslice_len(sc_tslice s);

/* Stale slices, a mismatched elem and an out-of-range index return false and
 * leave out[0 .. elem - 1] unchanged. out must not overlap the arena. */
/*@ requires valid_arena(a) && slice_storage(a, s.bytes);
    requires elem > 0 ==> \valid(out + (0 .. elem - 1));
    requires elem > 0 ==> \separated(out + (0 .. elem - 1),
                                     a, a->base + (0 .. a->cap - 1));
    terminates \true;
    exits \false;
    assigns out[0 .. elem - 1];
    ensures \result <==> valid_slice(a, s.bytes) && elem > 0 && s.elem == elem &&
                         i < s.bytes.len / elem;
    ensures \result ==> \forall integer k; 0 <= k < elem ==>
                          out[k] == \old(s.bytes.ptr[i * elem + k]);
    ensures !\result ==> \forall integer k; 0 <= k < elem ==>
                           out[k] == \old(out[k]);
*/
bool sc_tslice_get(const sc_arena *a, sc_tslice s, size_t i, size_t elem, uint8_t *out);

/* in must not overlap the array. */
/*@ requires valid_arena(a) && slice_storage(a, s.bytes);
    requires elem > 0 ==> \valid_read(in + (0 .. elem - 1));
    requires elem > 0 ==> \separated(in + (0 .. elem - 1),
                                     s.bytes.ptr + (0 .. s.bytes.len - 1));
    terminates \true;
    exits \false;
    assigns s.bytes.ptr[0 .. s.bytes.len - 1];
    ensures \result <==> valid_slice(a, s.bytes) && elem > 0 && s.elem == elem &&
                         i < s.bytes.len / elem;
    ensures \result ==> \forall integer k; 0 <= k < elem ==>
                          s.bytes.ptr[i * elem + k] == in[k];
    ensures \forall integer k; 0 <= k < s.bytes.len ==>
              (!\result || k < i * elem || k >= i * elem + elem) ==>
              s.bytes.ptr[k] == \old(s.bytes.ptr[k]);
*/
bool sc_tslice_set(const sc_arena *a, sc_tslice s, size_t i, size_t elem, const uint8_t *in);

/* Typed facade. The `1 ? (p) : (T *)0` form makes a pointer of any other type
 * a -Wpointer-type-mismatch error under -Werror; the cast to bytes comes after that check. */
#define SC_TSLICE_ALLOC(T, a, count) \
    ((void)sizeof(char[_Alignof(T) <= 8 ? 1 : -1]), sc_tslice_alloc((a), (count), sizeof(T)))
#define SC_TSLICE_GET(T, a, s, i, out) \
    sc_tslice_get((a), (s), (i), sizeof(T), (uint8_t *)(1 ? (out) : (T *)0))
#define SC_TSLICE_SET(T, a, s, i, in) \
    sc_tslice_set((a), (s), (i), sizeof(T), (const uint8_t *)(1 ? (in) : (const T *)0))

#endif
