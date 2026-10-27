#include "safec/arena.h"
#include "safec/buf.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static alignas(8) uint8_t storage[4096];
static alignas(8) uint8_t other_storage[256];
static sc_arena a, other;

/* A fresh builder over `cap` bytes, with every byte preloaded to '#'. */
static sc_buf fresh(size_t cap) {
    assert(sc_arena_reset(&a));
    sc_slice dst = sc_arena_alloc(&a, cap);
    assert(dst.len == cap);
    memset(dst.ptr, '#', cap);
    return sc_buf_new(dst);
}

static bool is(sc_buf b, const char *want) {
    sc_str v = sc_buf_view(&a, b);
    return !b.overflow && v.epoch == a.epoch && v.len == strlen(want) &&
           (v.len == 0 || memcmp(v.ptr, want, v.len) == 0);
}

static void expect_u64(uint64_t v, const char *plain, const char *sep) {
    sc_buf b = fresh(64);
    assert(sc_buf_u64(&a, &b, v) && is(b, plain));
    b = fresh(64);
    assert(sc_buf_u64_sep(&a, &b, v) && is(b, sep));
}

static void expect_ms(uint64_t ms, const char *want) {
    sc_buf b = fresh(64);
    assert(sc_buf_ms(&a, &b, ms));
    assert(is(b, want));
}

static void numbers(void) {
    expect_u64(0, "0", "0");
    expect_u64(9, "9", "9");
    expect_u64(10, "10", "10");
    expect_u64(999, "999", "999");
    expect_u64(1000, "1000", "1,000");
    expect_u64(182334, "182334", "182,334");
    expect_u64(1000000, "1000000", "1,000,000");
    expect_u64(UINT64_MAX, "18446744073709551615", "18,446,744,073,709,551,615");
}

static void durations(void) {
    expect_ms(0, "0 ms");
    expect_ms(999, "999 ms");
    expect_ms(1000, "1.0 s");
    expect_ms(1049, "1.0 s");
    expect_ms(1050, "1.1 s");
    expect_ms(59949, "59.9 s");
    expect_ms(59950, "1 min 0 s");
    expect_ms(59999, "1 min 0 s");
    expect_ms(60000, "1 min 0 s");
    expect_ms(184000, "3 min 4 s");
    expect_ms(3599499, "59 min 59 s");
    expect_ms(3599500, "60 min 0 s");
    expect_ms(UINT64_MAX, "307445734561825 min 52 s");
}

/* Exactly enough room succeeds; one byte less writes nothing and sticks. */
static void room_and_overflow(void) {
    sc_buf b = fresh(5);
    assert(SC_BUF_LIT(&a, &b, "12") && sc_buf_u64(&a, &b, 345) && is(b, "12345"));
    assert(b.len == 5);

    b = fresh(5);
    assert(SC_BUF_LIT(&a, &b, "12"));
    assert(!sc_buf_u64(&a, &b, 3456));
    assert(b.overflow && b.len == 2 && memcmp(b.dst.ptr, "12###", 5) == 0);
    /* Sticky: a later append that would fit still fails and writes nothing. */
    assert(!SC_BUF_LIT(&a, &b, "x") && !sc_buf_u64(&a, &b, 1) && !sc_buf_ms(&a, &b, 1));
    assert(b.len == 2 && memcmp(b.dst.ptr, "12###", 5) == 0);

    /* Separator output counts its commas: 1,000 is five bytes, not four. */
    b = fresh(4);
    assert(!sc_buf_u64_sep(&a, &b, 1000) && b.overflow && b.len == 0);
    b = fresh(5);
    assert(sc_buf_ms(&a, &b, 1000) && is(b, "1.0 s"));
    b = fresh(4);
    assert(!sc_buf_ms(&a, &b, 1000) && b.overflow && b.dst.ptr[0] == '#');

    /* Raw bytes: exact fit, one byte short, and a size of SIZE_MAX. */
    b = fresh(3);
    assert(SC_BUF_LIT(&a, &b, "abc") && is(b, "abc"));
    b = fresh(3);
    assert(SC_BUF_LIT(&a, &b, "a") && !SC_BUF_LIT(&a, &b, "bcd"));
    assert(b.overflow && b.len == 1 && memcmp(b.dst.ptr, "a##", 3) == 0);
    b = fresh(8);
    assert(SC_BUF_LIT(&a, &b, "ab") && !sc_buf_lit(&a, &b, storage, SIZE_MAX));
    assert(b.overflow && b.len == 2 && memcmp(b.dst.ptr, "ab######", 8) == 0);

    /* A length set past the destination by hand refuses every append and
     * views as empty, rather than writing out of bounds. */
    b = fresh(8);
    b.len = 12;
    assert(!SC_BUF_LIT(&a, &b, "x") && b.overflow && b.len == 12);
    assert(memcmp(b.dst.ptr, "########", 8) == 0);
    b.overflow = false;
    assert(!sc_buf_u64(&a, &b, 1) && b.overflow);
    sc_str v = sc_buf_view(&a, b);
    assert(v.ptr == NULL && v.len == 0);

    /* An empty append into a full builder succeeds and changes nothing. */
    b = fresh(1);
    assert(SC_BUF_LIT(&a, &b, "z") && sc_buf_lit(&a, &b, NULL, 0) && is(b, "z"));
}

static void strings(void) {
    sc_buf b = fresh(32);
    sc_str here = SC_STR_LIT(&a, "rows");
    sc_str there = SC_STR_LIT(&other, " here");
    assert(sc_buf_u64(&a, &b, 3) && SC_BUF_LIT(&a, &b, " ") && sc_buf_str(&a, &b, &a, here));
    assert(sc_buf_str(&a, &b, &other, there) && is(b, "3 rows here"));

    /* Strings: exact fit, one byte short, then sticky. */
    sc_buf c = fresh(4);
    sc_str four = SC_STR_LIT(&other, "abcd");
    assert(sc_buf_str(&a, &c, &other, four) && is(c, "abcd"));
    c = fresh(4);
    assert(SC_BUF_LIT(&a, &c, "x") && !sc_buf_str(&a, &c, &other, four));
    assert(c.overflow && c.len == 1 && memcmp(c.dst.ptr, "x###", 4) == 0);
    sc_str one = SC_STR_LIT(&other, "y");
    assert(!sc_buf_str(&a, &c, &other, one) && c.len == 1);

    /* A reset of the source's arena leaves the builder over `a` working. */
    sc_buf d = fresh(8);
    assert(SC_BUF_LIT(&a, &d, "ok"));
    assert(sc_arena_reset(&other));
    assert(SC_BUF_LIT(&a, &d, "!") && is(d, "ok!"));
    there = SC_STR_LIT(&other, " here");

    /* A stale source string fails like a full builder. */
    assert(sc_arena_reset(&other));
    size_t len = b.len;
    assert(!sc_buf_str(&a, &b, &other, there) && b.overflow && b.len == len);
}

static void stale(void) {
    sc_buf b = fresh(16);
    assert(SC_BUF_LIT(&a, &b, "ok"));
    assert(sc_arena_reset(&a));
    assert(!SC_BUF_LIT(&a, &b, "x") && b.overflow && b.len == 2);
    sc_buf c = sc_buf_new(b.dst);
    assert(!sc_buf_u64(&a, &c, 1) && !sc_buf_u64_sep(&a, &c, 1) && !sc_buf_ms(&a, &c, 1));
    c = sc_buf_new(b.dst);
    assert(!sc_buf_str(&a, &c, &a, (sc_str){NULL, 0, a.epoch}) && c.overflow);
    /* The view of a stale or empty builder is the canonical empty string. */
    sc_str v = sc_buf_view(&a, b);
    assert(v.ptr == NULL && v.len == 0 && v.epoch == a.epoch);
    v = sc_buf_view(&a, fresh(8));
    assert(v.ptr == NULL && v.len == 0 && v.epoch == a.epoch);
}

static void zero_capacity(void) {
    assert(sc_arena_reset(&a));
    sc_slice none = sc_arena_alloc(&a, 0);
    assert(none.ptr == NULL && none.len == 0);
    sc_buf z = sc_buf_new(none);
    sc_str empty = {NULL, 0, other.epoch};
    assert(sc_buf_lit(&a, &z, NULL, 0) && SC_BUF_LIT(&a, &z, "") && sc_buf_str(&a, &z, &other, empty));
    assert(z.len == 0 && !z.overflow && is(z, ""));
    assert(!SC_BUF_LIT(&a, &z, "x") && z.overflow && z.len == 0);
    assert(!sc_buf_str(&a, &z, &other, empty) && z.overflow);
}

int main(void) {
    sc_arena_init(&a, storage, sizeof storage);
    sc_arena_init(&other, other_storage, sizeof other_storage);
    sc_buf n = sc_buf_new((sc_slice){NULL, 0, 7});
    assert(n.len == 0 && !n.overflow && n.dst.epoch == 7);
    numbers();
    durations();
    room_and_overflow();
    strings();
    stale();
    zero_capacity();
    puts("buf_test: ok");
    return 0;
}
