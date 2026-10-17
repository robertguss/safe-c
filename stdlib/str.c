#include "safec/arena.h"
#include "safec/str.h"

/*@ requires valid_arena(a) && slice_storage(a, s);
    assigns \nothing;
    ensures \result <==> valid_slice(a, s);
*/
static bool str_live(const sc_arena *a, sc_str s) {
    return s.epoch == a->epoch &&
           (s.len == 0 || (s.len <= a->used && s.ptr >= a->base &&
                          s.ptr <= a->base + (a->used - s.len)));
}

/*@ requires hay.len == 0 || \valid_read(hay.ptr + (0 .. hay.len - 1));
    requires needle.len == 0 || \valid_read(needle.ptr + (0 .. needle.len - 1));
    requires i + needle.len <= hay.len;
    assigns \nothing;
    ensures \result <==> str_match_at(hay, needle, i);
*/
static bool match_at(sc_str hay, sc_str needle, size_t i) {
    /*@ loop invariant 0 <= k <= needle.len;
        loop invariant \forall integer j; 0 <= j < k ==> hay.ptr[i + j] == needle.ptr[j];
        loop assigns k;
        loop variant needle.len - k;
    */
    for (size_t k = 0; k < needle.len; k++) {
        if (hay.ptr[i + k] != needle.ptr[k])
            return false;
    }
    return true;
}

sc_str sc_str_from_lit(sc_arena *a, const uint8_t *lit, size_t n) {
    sc_str s = sc_arena_alloc(a, n);
    /*@ loop invariant 0 <= i <= s.len;
        loop invariant \forall integer k; 0 <= k < i ==> s.ptr[k] == lit[k];
        loop assigns i, s.ptr[0 .. s.len - 1];
        loop variant s.len - i;
    */
    for (size_t i = 0; i < s.len; i++)
        s.ptr[i] = lit[i];
    return s;
}

bool sc_str_eq(const sc_arena *a, sc_str x, sc_str y) {
    if (!str_live(a, x) || !str_live(a, y) || x.len != y.len)
        return false;
    /*@ loop invariant 0 <= i <= x.len;
        loop invariant \forall integer k; 0 <= k < i ==> x.ptr[k] == y.ptr[k];
        loop assigns i;
        loop variant x.len - i;
    */
    for (size_t i = 0; i < x.len; i++) {
        if (x.ptr[i] != y.ptr[i])
            return false;
    }
    return true;
}

bool sc_str_find(const sc_arena *a, sc_str hay, sc_str needle, size_t *out) {
    if (!str_live(a, hay) || !str_live(a, needle) || needle.len > hay.len)
        return false;
    size_t last = hay.len - needle.len;
    /*@ loop invariant 0 <= i <= last + 1;
        loop invariant \forall integer j; 0 <= j < i ==> !str_match_at(hay, needle, j);
        loop assigns i;
        loop variant last + 1 - i;
    */
    for (size_t i = 0; i <= last; i++) {
        if (match_at(hay, needle, i)) {
            *out = i;
            return true;
        }
    }
    return false;
}
