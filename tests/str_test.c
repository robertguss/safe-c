#include "safec/arena.h"
#include "safec/str.h"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

int main(void) {
    alignas(8) uint8_t storage[64] = {0};
    alignas(8) uint8_t other_storage[64] = {0};
    sc_arena a, other;
    sc_arena_init(&a, storage, sizeof storage);
    sc_arena_init(&other, other_storage, sizeof other_storage);

    /* from_lit: content, empty literal, full arena */
    sc_str empty = SC_STR_LIT(&a, "");
    assert(empty.ptr == NULL && empty.len == 0 && a.used == 0);
    sc_str hello = SC_STR_LIT(&a, "hello world");
    assert(hello.len == 11 && memcmp(hello.ptr, "hello world", 11) == 0 && a.used == 16);
    sc_str bytes = sc_str_from_lit(&a, (const uint8_t *)"a\0b", 3);
    assert(bytes.len == 3 && memcmp(bytes.ptr, "a\0b", 3) == 0 && a.used == 24);
    sc_str big = sc_str_from_lit(&a, storage, SIZE_MAX);
    assert(big.ptr == NULL && big.len == 0 && a.used == 24);
    sc_str big2 = sc_str_from_lit(&a, storage, 41);
    assert(big2.ptr == NULL && big2.len == 0 && a.used == 24);
    sc_str last = sc_str_from_lit(&a, (const uint8_t *)"0123456789012345678901234567890123456789", 40);
    assert(last.len == 40 && a.used == 64 && memcmp(last.ptr, "0123456789012345678901234567890123456789", 40) == 0);
    sc_str none = SC_STR_LIT(&a, "x");
    assert(none.ptr == NULL && none.len == 0 && a.used == 64);

    /* eq: equal, unequal by byte, unequal by length, empty */
    sc_arena_init(&other, other_storage, sizeof other_storage);
    sc_str x = SC_STR_LIT(&other, "abcd");
    sc_str y = SC_STR_LIT(&other, "abcd");
    sc_str z = SC_STR_LIT(&other, "abce");
    sc_str w = SC_STR_LIT(&other, "abc");
    assert(sc_str_eq(&other, x, x));
    assert(sc_str_eq(&other, x, y));
    assert(!sc_str_eq(&other, x, z));
    assert(!sc_str_eq(&other, z, x));
    assert(!sc_str_eq(&other, x, w));
    assert(!sc_str_eq(&other, w, x));
    sc_str none_o = sc_slice_sub(&other, x, 0, 0);
    assert(none_o.len == 0);
    assert(sc_str_eq(&other, none_o, none_o));
    assert(!sc_str_eq(&other, none_o, x));
    assert(sc_str_eq(&other, sc_slice_sub(&other, x, 1, 3), sc_slice_sub(&other, y, 1, 3)));

    /* find: found at start, middle, end; not found; empty needle; too long */
    sc_arena_init(&other, other_storage, sizeof other_storage);
    sc_str hay = SC_STR_LIT(&other, "abcabd");
    sc_str ab = SC_STR_LIT(&other, "ab");
    sc_str bd = SC_STR_LIT(&other, "bd");
    sc_str cab = SC_STR_LIT(&other, "cab");
    sc_str zz = SC_STR_LIT(&other, "zz");
    size_t at = 99;
    assert(sc_str_find(&other, hay, ab, &at) && at == 0);
    assert(sc_str_find(&other, hay, bd, &at) && at == 4);
    assert(sc_str_find(&other, hay, cab, &at) && at == 2);
    at = 77;
    assert(!sc_str_find(&other, hay, zz, &at) && at == 77);
    assert(sc_str_find(&other, hay, sc_slice_sub(&other, hay, 0, 6), &at) && at == 0);
    assert(sc_str_find(&other, hay, none_o, &at) && at == 0);
    sc_str longer = sc_slice_sub(&other, hay, 0, 6);
    at = 77;
    assert(!sc_str_find(&other, sc_slice_sub(&other, hay, 0, 2), longer, &at) && at == 77);
    assert(sc_str_find(&other, none_o, none_o, &at) && at == 0);
    at = 77;
    assert(!sc_str_find(&other, none_o, ab, &at) && at == 77);

    /* stale after reset of the owner; still live after reset of another arena */
    assert(sc_arena_reset(&a));
    assert(sc_str_eq(&other, ab, ab));
    assert(sc_arena_reset(&other));
    at = 77;
    assert(!sc_str_eq(&other, ab, ab));
    assert(!sc_str_eq(&other, none_o, none_o));
    assert(!sc_str_find(&other, hay, ab, &at) && at == 77);
    assert(!sc_str_find(&other, hay, none_o, &at) && at == 77);
    assert(!sc_str_eq(&a, hello, hello));
    return 0;
}
