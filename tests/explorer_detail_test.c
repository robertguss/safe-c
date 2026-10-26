/* Tests the row-detail pane's core (examples/explorer/detail.c): type names
 * and wrapping at character boundaries. Core code, built in. */
#include "../examples/explorer/detail.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static alignas(8) uint8_t mem[1u << 14];
static sc_arena a;

static sc_str lit(const char *s) {
    return sc_str_from_lit(&a, (const uint8_t *)s, strlen(s));
}

static bool is(sc_str s, const char *want) {
    return s.len == strlen(want) && (s.len == 0 || memcmp(s.ptr, want, s.len) == 0);
}

/* One point a byte, so widths are byte counts. */
static uint32_t bytes(void *ctx, const sc_arena *sa, sc_str s) {
    (void)ctx;
    (void)sa;
    return (uint32_t)s.len;
}

static size_t wrap(const char *v, uint32_t width, sc_tslice lines, bool *more) {
    size_t n = 0;
    assert(detail_wrap(&a, lit(v), width, bytes, NULL, &a, lines, &n, more));
    return n;
}

static sc_str line(sc_tslice lines, size_t i) {
    sc_str s;
    assert(SC_TSLICE_GET(sc_str, &a, lines, i, &s));
    return s;
}

int main(void) {
    sc_arena_init(&a, mem, sizeof mem);
    sc_tslice lines = SC_TSLICE_ALLOC(sc_str, &a, 8);
    bool more = true;
    /* A width that ends inside the emoji breaks before it. */
    assert(wrap("aaaa\xf0\x9f\x98\x80" "bbbb", 6, lines, &more) == 3 && !more);
    assert(is(line(lines, 0), "aaaa") && is(line(lines, 1), "\xf0\x9f\x98\x80" "bb") &&
           is(line(lines, 2), "bb"));
    /* A newline starts a new line there and is not shown. */
    assert(wrap("ab\ncd", 10, lines, &more) == 2);
    assert(is(line(lines, 0), "ab") && is(line(lines, 1), "cd"));
    assert(wrap("a\n\nb", 10, lines, &more) == 3 && is(line(lines, 1), ""));
    /* A character wider than the pane still gets a line of its own. */
    assert(wrap("\xf0\x9f\x98\x80\xf0\x9f\x98\x80", 2, lines, &more) == 2);
    assert(is(line(lines, 0), "\xf0\x9f\x98\x80"));
    /* Words: break after the last space, keeping it on the first line. */
    assert(wrap("ab cd ef", 6, lines, &more) == 2);
    assert(is(line(lines, 0), "ab cd ") && is(line(lines, 1), "ef"));
    assert(wrap("one twothree", 6, lines, &more) == 3 && is(line(lines, 0), "one ") &&
           is(line(lines, 1), "twothr") && is(line(lines, 2), "ee"));
    /* Exactly the width fits. */
    assert(wrap("abcdef", 3, lines, &more) == 2 && is(line(lines, 0), "abc"));
    /* An invalid byte breaks as a one-byte character. */
    assert(wrap("ab\xff" "cd", 3, lines, &more) == 2 && is(line(lines, 0), "ab\xff"));
    /* Empty: one empty line. Too many lines: the ones that fit, and more. */
    sc_str none = {NULL, 0, a.epoch};
    size_t n = 0;
    assert(detail_wrap(&a, none, 5, bytes, NULL, &a, lines, &n, &more) && n == 1 && !more);
    assert(wrap("abcdefghijklmnopqrstuvwxyz", 2, lines, &more) == 8 && more);
    assert(is(line(lines, 7), "op"));
    /* Stale input fails. */
    sc_str gone = lit("gone");
    alignas(8) static uint8_t other_mem[256];
    sc_arena other;
    sc_arena_init(&other, other_mem, sizeof other_mem);
    sc_str elsewhere = sc_str_from_lit(&other, (const uint8_t *)"x", 1);
    assert(sc_arena_reset(&other));
    assert(!detail_wrap(&other, elsewhere, 5, bytes, NULL, &a, lines, &n, &more));
    (void)gone;
    /* Type names. */
    assert(is(detail_type_name(&a, 23), "integer") && is(detail_type_name(&a, 25), "text") &&
           is(detail_type_name(&a, 1184), "timestamp with time zone") &&
           is(detail_type_name(&a, 16), "boolean") && is(detail_type_name(&a, 1700), "numeric"));
    assert(is(detail_type_name(&a, 99999), "99999") && is(detail_type_name(&a, 0), "0"));
    puts("explorer_detail_test: ok");
    return 0;
}
