#include "../examples/explorer/proto.c"
#include "../examples/explorer/result.c"
#include "../examples/explorer/copy.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static alignas(8) uint8_t source[4096], output[4096], columns[4096], chunks[4096], index_mem[65536];

static void field(sc_arena *a, sc_arena *out, const char *s, bool null, bool numeric,
                  bool csv, const char *expected) {
    assert(sc_arena_reset(a) && sc_arena_reset(out));
    sc_str text = sc_str_from_lit(a, (const uint8_t *)s, strlen(s));
    sc_buf b = sc_buf_new(sc_arena_alloc(out, 1024));
    assert(copy_field(out, &b, a, text, null, numeric, csv));
    assert(b.len == strlen(expected) && memcmp(b.dst.ptr, expected, b.len) == 0);
}

int main(void) {
    sc_arena a, out, ca, ch, book;
    sc_arena_init(&a, source, sizeof source);
    sc_arena_init(&out, output, sizeof output);
    field(&a, &out, "", true, false, true, "");
    field(&a, &out, "", false, false, true, "\"\"");
    field(&a, &out, "a\"b", false, false, true, "\"a\"\"b\"");
    field(&a, &out, "42", false, true, true, "42");
    field(&a, &out, "42", false, false, true, "\"42\"");
    field(&a, &out, "a\tb\nc\rd", false, false, false, "a b c d");
    sc_arena_init(&ca, columns, sizeof columns);
    sc_arena_init(&ch, chunks, sizeof chunks);
    sc_arena_init(&book, index_mem, sizeof index_mem);
    assert(sc_arena_reset(&a));
    sc_str x = SC_STR_LIT(&a, "x"), y = SC_STR_LIT(&a, "y");
    sc_str empty = {NULL, 0, a.epoch};
    proto_writer w;
    proto_header h = {PROTO_COLUMNS, 1, 1, 0, 0, 0};
    assert(proto_begin(&w, &ca, h) && proto_put_u64(&w, 2));
    assert(proto_put_str(&w, &a, x) && proto_put_u64(&w, 25));
    assert(proto_put_str(&w, &a, y) && proto_put_u64(&w, 25) && proto_end(&w));
    h.kind = PROTO_CHUNK;
    h.arg = 2;
    assert(proto_begin(&w, &ch, h));
    assert(proto_put_u64(&w, PROTO_CELL_NULL) && proto_put_str(&w, &a, empty));
    assert(proto_put_u64(&w, 0) && proto_put_str(&w, &a, empty));
    sc_str ab = SC_STR_LIT(&a, "a\tb"), cd = SC_STR_LIT(&a, "c\nd");
    assert(proto_put_u64(&w, PROTO_CELL_TRUNCATED | 8) && proto_put_str(&w, &a, ab));
    assert(proto_put_u64(&w, PROTO_CELL_TRUNCATED | 9) && proto_put_str(&w, &a, cd) && proto_end(&w));
    result r;
    assert(result_init(&r, &book, 1) && result_set_columns(&r, &ca, 0) == RESULT_ADDED);
    assert(result_add_chunk(&r, &ch, 1) == RESULT_ADDED);
    sc_str text;
    uint64_t truncated = 99;
    assert(sc_arena_reset(&out));
    assert(copy_rows(&out, 4096, &r, 0, 1, false, &text, &truncated));
    assert(text.len == 9 && memcmp(text.ptr, "\t\na b\tc d", 9) == 0 && truncated == 2);
    assert(sc_arena_reset(&out));
    assert(copy_rows(&out, 4096, &r, 0, 1, true, &text, &truncated));
    const char *csv = "x,y\n,\"\"\n\"a\tb\",\"c\nd\"";
    assert(text.len == strlen(csv) && memcmp(text.ptr, csv, text.len) == 0 && truncated == 2);
    assert(sc_arena_reset(&out));
    text = x;
    truncated = 99;
    assert(!copy_rows(&out, 8, &r, 0, 1, false, &text, &truncated));
    assert(text.ptr == x.ptr && truncated == 99);
    assert(sc_arena_reset(&out));
    assert(copy_rows(&out, 4096, &r, 1, 1, false, &text, &truncated));
    assert(text.len == 7 && memcmp(text.ptr, "a b\tc d", 7) == 0);
    puts("explorer_copy_test: ok");
}
