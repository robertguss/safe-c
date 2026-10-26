#include "safec/arena.h"
#include "safec/utf8.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static alignas(8) uint8_t storage[4096];
static sc_arena a;

static sc_str lit(const char *s) {
    size_t n = strlen(s);
    return n == 0 ? (sc_str){NULL, 0, a.epoch} : sc_str_from_lit(&a, (const uint8_t *)s, n);
}

static void decodes(const char *s, uint32_t cp, size_t len) {
    sc_str t = lit(s);
    sc_utf8_char c = sc_utf8_next(&a, t, 0);
    assert(c.valid && c.cp == cp && c.len == len && len == t.len);
}

static void invalid(const char *s) {
    sc_utf8_char c = sc_utf8_next(&a, lit(s), 0);
    assert(!c.valid && c.cp == 0 && c.len == 1);
}

static void next(void) {
    assert(sc_arena_reset(&a));
    decodes("A", 0x41, 1);
    decodes("\xc3\xa9", 0xE9, 2);             /* é */
    decodes("\xe2\x82\xac", 0x20AC, 3);       /* € */
    decodes("\xf0\x9f\x98\x80", 0x1F600, 4);  /* 😀 */
    /* Each edge of each range, valid side. */
    decodes("\x7f", 0x7F, 1);
    decodes("\xc2\x80", 0x80, 2);
    decodes("\xdf\xbf", 0x7FF, 2);
    decodes("\xe0\xa0\x80", 0x800, 3);
    decodes("\xed\x9f\xbf", 0xD7FF, 3);
    decodes("\xee\x80\x80", 0xE000, 3);
    decodes("\xef\xbf\xbf", 0xFFFF, 3);
    decodes("\xf0\x90\x80\x80", 0x10000, 4);
    decodes("\xf3\xbf\xbf\xbf", 0xFFFFF, 4);
    decodes("\xf4\x8f\xbf\xbf", 0x10FFFF, 4);
    /* And the invalid side: overlongs, surrogates, past U+10FFFF, stray bytes. */
    invalid("\xc0\x80");
    invalid("\xc1\xbf");
    invalid("\xe0\x80\x80");
    invalid("\xe0\x9f\xbf");
    invalid("\xed\xa0\x80");
    invalid("\xed\xbf\xbf");
    invalid("\xf0\x8f\xbf\xbf");
    invalid("\xf4\x90\x80\x80");
    invalid("\xf5\x80\x80\x80");
    invalid("\xff");
    invalid("\x80");
    invalid("\xbf");
    invalid("\xc3\x41");             /* a lead byte without its continuation */
    invalid("\xe2\x82\x41");
    invalid("\xf0\x9f\x98\x41");

    /* Cut short by the end of the string: the next byte in memory would
     * complete it, so reading past the end would show as a valid sequence. */
    sc_str euro = lit("\xe2\x82\xac");
    sc_str cut = {euro.ptr, 2, euro.epoch};
    sc_utf8_char c = sc_utf8_next(&a, cut, 0);
    assert(!c.valid && c.len == 1 && c.cp == 0);
    c = sc_utf8_next(&a, cut, 1); /* a lone continuation byte */
    assert(!c.valid && c.len == 1);

    /* In the middle of a string, at its end, past it, and stale. */
    sc_str mixed = lit("a\xc3\xa9z");
    c = sc_utf8_next(&a, mixed, 1);
    assert(c.valid && c.cp == 0xE9 && c.len == 2);
    c = sc_utf8_next(&a, mixed, 3);
    assert(c.valid && c.cp == 'z' && c.len == 1);
    c = sc_utf8_next(&a, mixed, 4);
    assert(!c.valid && c.len == 0 && c.cp == 0);
    c = sc_utf8_next(&a, mixed, 99);
    assert(!c.valid && c.len == 0);
    assert(sc_arena_reset(&a));
    c = sc_utf8_next(&a, mixed, 0);
    assert(!c.valid && c.len == 0 && c.cp == 0);
}

static void valid_and_count(void) {
    assert(sc_arena_reset(&a));
    sc_str all = lit("a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80"); /* aé€😀 */
    assert(sc_utf8_valid(&a, all) && sc_utf8_count(&a, all) == 4);
    sc_str empty = lit("");
    assert(sc_utf8_valid(&a, empty) && sc_utf8_count(&a, empty) == 0);
    /* An invalid byte counts as one step, wherever it sits. */
    sc_str bad = lit("a\x80" "b");
    assert(!sc_utf8_valid(&a, bad) && sc_utf8_count(&a, bad) == 3);
    sc_str tail = lit("ab\xe2\x82"); /* cut short at the end: two steps of one byte */
    assert(!sc_utf8_valid(&a, tail) && sc_utf8_count(&a, tail) == 4);
    sc_str head = lit("\xed\xa0\x80z"); /* a surrogate first: three bad bytes, then z */
    assert(!sc_utf8_valid(&a, head) && sc_utf8_count(&a, head) == 4);
    assert(sc_arena_reset(&a));
    assert(!sc_utf8_valid(&a, all) && sc_utf8_count(&a, all) == 0);
    assert(!sc_utf8_valid(&a, empty)); /* stale, even though empty */
}

static void prev(void) {
    assert(sc_arena_reset(&a));
    sc_str s = lit("a\xf0\x9f\x98\x80"); /* a😀 */
    assert(sc_utf8_prev(&a, s, 5) == 1);
    assert(sc_utf8_prev(&a, s, 1) == 0);
    assert(sc_utf8_prev(&a, s, 3) == 1); /* from inside the sequence */
    assert(sc_utf8_prev(&a, s, 0) == 0);
    assert(sc_utf8_prev(&a, s, 6) == 0); /* past the end */
    sc_str e = lit("x\xc3\xa9"); /* xé */
    assert(sc_utf8_prev(&a, e, 3) == 1);
    /* Continuation bytes only: the nearest boundary within four bytes wins,
     * else one byte back. */
    sc_str run = lit("ab\x80\x80\x80\x80\x80");
    assert(sc_utf8_prev(&a, run, 5) == 1);
    assert(sc_utf8_prev(&a, run, 6) == 5);
    assert(sc_utf8_prev(&a, run, 7) == 6);
    sc_str lone = lit("\x80\x80");
    assert(sc_utf8_prev(&a, lone, 2) == 0); /* offset 0 is always a boundary */
    assert(sc_arena_reset(&a));
    assert(sc_utf8_prev(&a, s, 5) == 0);
}

static sc_str cut(sc_str s, size_t max) {
    sc_str t = sc_utf8_truncate(&a, s, max);
    assert(t.ptr == s.ptr && t.epoch == s.epoch && t.len <= s.len);
    return t;
}

static void truncate(void) {
    assert(sc_arena_reset(&a));
    sc_str s = lit("a\xc3\xa9\xe2\x82\xac"); /* aé€: offsets 0, 1, 3 */
    assert(cut(s, 3).len == 3);
    assert(cut(s, 1).len == 1);
    assert(cut(s, 2).len == 1);
    assert(cut(s, 4).len == 3);
    assert(cut(s, 5).len == 3);
    assert(cut(s, 6).len == 6);
    assert(cut(s, 100).len == 6);
    assert(cut(s, 0).len == 0);
    for (size_t m = 0; m <= 7; m++) {
        assert(sc_utf8_valid(&a, sc_utf8_truncate(&a, s, m)));
    }
    /* Five continuation bytes: no boundary in max - 3 .. max, so cut at max. */
    sc_str run = lit("aaaa\x80\x80\x80\x80\x80");
    assert(cut(run, 8).len == 8);
    assert(cut(run, 7).len == 7);
    assert(cut(run, 6).len == 3); /* 3 is 'a', within three bytes of 6 */
    assert(sc_arena_reset(&a));
    sc_str t = sc_utf8_truncate(&a, s, 3);
    assert(t.ptr == NULL && t.len == 0 && t.epoch == a.epoch);
}

int main(void) {
    sc_arena_init(&a, storage, sizeof storage);
    next();
    valid_and_count();
    prev();
    truncate();
    puts("utf8_test: ok");
    return 0;
}
