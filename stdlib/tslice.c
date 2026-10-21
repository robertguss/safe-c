#include "safec/tslice.h"

/*@ requires valid_arena(a) && slice_storage(a, s.bytes);
    assigns \nothing;
    ensures \result <==> valid_slice(a, s.bytes) && elem > 0 && s.elem == elem &&
                         i < s.bytes.len / elem;
*/
static bool elem_live(const sc_arena *a, sc_tslice s, size_t i, size_t elem) {
    return s.bytes.epoch == a->epoch && elem > 0 && s.elem == elem &&
           i < s.bytes.len / elem &&
           (s.bytes.len <= a->used && s.bytes.ptr >= a->base &&
            s.bytes.ptr <= a->base + (a->used - s.bytes.len));
}

sc_tslice sc_tslice_alloc(sc_arena *a, size_t count, size_t elem) {
    sc_tslice empty = {{NULL, 0, a->epoch}, 0};
    if (elem == 0 || count > SIZE_MAX / elem)
        return empty;
    sc_slice bytes = sc_arena_alloc(a, count * elem);
    if (bytes.len == 0)
        return empty;
    sc_tslice result = {bytes, elem};
    return result;
}

size_t sc_tslice_len(sc_tslice s) {
    return s.elem == 0 ? 0 : s.bytes.len / s.elem;
}

/*@ requires \valid(dst + (0 .. n - 1)) && \valid_read(src + (0 .. n - 1));
    requires \separated(dst + (0 .. n - 1), src + (0 .. n - 1));
    terminates \true;
    exits \false;
    assigns dst[0 .. n - 1];
    ensures \forall integer k; 0 <= k < n ==> dst[k] == src[k];
*/
static void copy_bytes(uint8_t *dst, const uint8_t *src, size_t n) {
    /*@ loop invariant 0 <= k <= n;
        loop invariant \forall integer j; 0 <= j < k ==> dst[j] == src[j];
        loop assigns k, dst[0 .. n - 1];
        loop variant n - k;
    */
    for (size_t k = 0; k < n; k++)
        dst[k] = src[k];
}

bool sc_tslice_get(const sc_arena *a, sc_tslice s, size_t i, size_t elem, void *out) {
    if (!elem_live(a, s, i, elem))
        return false;
    copy_bytes(out, s.bytes.ptr + i * elem, elem);
    return true;
}

bool sc_tslice_set(const sc_arena *a, sc_tslice s, size_t i, size_t elem, const void *in) {
    if (!elem_live(a, s, i, elem))
        return false;
    copy_bytes(s.bytes.ptr + i * elem, in, elem);
    return true;
}
