#include "safec/arena.h"

/*@ requires valid_arena(a) && slice_storage(a, s);
    assigns \nothing;
    ensures \result <==> valid_slice(a, s);
*/
static bool slice_live(const sc_arena *a, sc_slice s) {
    return s.epoch == a->epoch &&
           (s.len == 0 || (s.len <= a->used && s.ptr >= a->base &&
                          s.ptr <= a->base + (a->used - s.len)));
}

bool sc_slice_get(const sc_arena *a, sc_slice s, size_t i, uint8_t *out) {
    if (!slice_live(a, s) || i >= s.len)
        return false;
    *out = s.ptr[i];
    return true;
}

sc_slice sc_slice_sub(const sc_arena *a, sc_slice s, size_t start, size_t n) {
    sc_slice empty = {NULL, 0, a->epoch};
    if (!slice_live(a, s) || start > s.len || n > s.len - start || n == 0)
        return empty;
    sc_slice result = {s.ptr + start, n, s.epoch};
    return result;
}

bool sc_slice_copy(const sc_arena *a, sc_slice dst, sc_slice src) {
    if (!slice_live(a, dst) || !slice_live(a, src) || src.len > dst.len)
        return false;
    /*@ loop invariant 0 <= i <= src.len;
        loop invariant \forall integer k; 0 <= k < i ==> dst.ptr[k] == \at(src.ptr[k], Pre);
        loop assigns i, dst.ptr[0 .. src.len - 1];
        loop variant src.len - i;
    */
    for (size_t i = 0; i < src.len; i++)
        dst.ptr[i] = src.ptr[i];
    return true;
}

bool sc_slice_move(const sc_arena *a, sc_slice dst, sc_slice src) {
    if (!slice_live(a, dst) || !slice_live(a, src) || src.len > dst.len)
        return false;
    if (src.len == 0)
        return true;
    if (dst.ptr <= src.ptr) {
        /*@ loop invariant 0 <= i <= src.len;
            loop invariant \forall integer k; 0 <= k < i ==> dst.ptr[k] == \at(src.ptr[k], Pre);
            loop invariant \forall integer k; i <= k < src.len ==> src.ptr[k] == \at(src.ptr[k], Pre);
            loop assigns i, dst.ptr[0 .. src.len - 1];
            loop variant src.len - i;
        */
        for (size_t i = 0; i < src.len; i++)
            dst.ptr[i] = src.ptr[i];
    } else {
        size_t i = src.len;
        /*@ loop invariant 0 <= i <= src.len;
            loop invariant \forall integer k; i <= k < src.len ==> dst.ptr[k] == \at(src.ptr[k], Pre);
            loop invariant \forall integer k; 0 <= k < i ==> src.ptr[k] == \at(src.ptr[k], Pre);
            loop assigns i, dst.ptr[0 .. src.len - 1];
            loop variant i;
        */
        while (i > 0) {
            i--;
            dst.ptr[i] = src.ptr[i];
        }
    }
    return true;
}
