/* Tests the explorer's result model (examples/explorer/result.c): chunk
 * lookup, ordering, the quota, cell reads, close and the column widths. Core
 * code, built in. */
#include "../examples/explorer/proto.c"
#include "../examples/explorer/result.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENAS 8u
#define ARENA_BYTES (256u * 1024u)

static alignas(8) uint8_t pool_mem[ARENAS][ARENA_BYTES];
static alignas(8) uint8_t book_mem[1u << 18];
static alignas(8) uint8_t out_mem[4096];
static sc_arena pool[ARENAS], book, oa;

static sc_arena *fresh(size_t i) {
    sc_arena_init(&pool[i], pool_mem[i], ARENA_BYTES);
    return &pool[i];
}

static sc_str lit(sc_arena *a, const char *s) {
    size_t n = strlen(s);
    return n == 0 ? (sc_str){NULL, 0, a->epoch} : sc_str_from_lit(a, (const uint8_t *)s, n);
}

/* COLUMNS in arena i: `n` columns named "c0".."c<n-1>", or the given names. */
static void columns(size_t i, uint64_t n, const char *const *names, const uint32_t *oids) {
    sc_arena *a = fresh(i);
    alignas(8) static uint8_t tmp_mem[4096];
    sc_arena tmp;
    sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
    proto_writer w;
    proto_header h = {PROTO_COLUMNS, 1, 1, 0, 0, 0};
    assert(proto_begin(&w, a, h) && proto_put_u64(&w, n));
    for (uint64_t c = 0; c < n; c++) {
        char name[8] = {'c', (char)('0' + c % 10), 0};
        assert(proto_put_str(&w, &tmp, lit(&tmp, names ? names[c] : name)) &&
               proto_put_u64(&w, oids ? oids[c] : 25u));
    }
    assert(proto_end(&w));
}

/* A CHUNK in arena i of `rows` rows with `cols` cells each: cell (r, 0) is the
 * decimal of first + r, the others "x". */
static void chunk(size_t i, uint64_t first, uint64_t rows, uint64_t cols) {
    sc_arena *a = fresh(i);
    alignas(8) static uint8_t tmp_mem[256];
    sc_arena tmp;
    proto_writer w;
    proto_header h = {PROTO_CHUNK, 1, 1, rows, 0, 0};
    assert(proto_begin(&w, a, h));
    for (uint64_t r = 0; r < rows; r++) {
        for (uint64_t c = 0; c < cols; c++) {
            char num[24];
            snprintf(num, sizeof num, "%llu", (unsigned long long)(first + r));
            sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
            sc_str s = lit(&tmp, c == 0 ? num : "x");
            assert(proto_put_u64(&w, s.len) && proto_put_str(&w, &tmp, s));
        }
    }
    assert(proto_end(&w));
}

static uint64_t first_cell(const result *r, uint64_t row) {
    proto_cell c;
    assert(result_cell(r, row, 0, &c) && !c.null);
    char buf[24] = {0};
    memcpy(buf, c.bytes.ptr, c.bytes.len);
    return strtoull(buf, NULL, 10);
}

static void lookup(void) {
    result r;
    assert(sc_arena_reset(&book) && result_init(&r, &book, 4));
    columns(0, 2, NULL, NULL);
    assert(result_set_columns(&r, &pool[0], 0) == RESULT_ADDED);
    chunk(1, 0, 100, 2);
    chunk(2, 100, 37, 2);
    chunk(3, 137, 4000, 2);
    assert(result_add_chunk(&r, &pool[1], 1, 0) == RESULT_ADDED);
    assert(result_add_chunk(&r, &pool[2], 2, 100) == RESULT_ADDED);
    assert(result_add_chunk(&r, &pool[3], 3, 137) == RESULT_ADDED);
    assert(r.rows == 4137);
    const uint64_t rows[] = {0, 99, 100, 136, 137, 4136};
    const size_t chunks[] = {0, 0, 1, 1, 2, 2};
    const uint64_t offs[] = {0, 99, 0, 36, 0, 3999};
    for (size_t k = 0; k < 6; k++) {
        size_t ch = 9;
        uint64_t off = 9;
        assert(result_find(&r, rows[k], &ch, &off) && ch == chunks[k] && off == offs[k]);
        assert(first_cell(&r, rows[k]) == rows[k]);
    }
    size_t ch = 0;
    uint64_t off = 0;
    assert(!result_find(&r, 4137, &ch, &off));
    proto_cell c;
    assert(!result_cell(&r, 4137, 0, &c) && !result_cell(&r, 5, 2, &c));
    assert(result_cell(&r, 5, 1, &c) && c.bytes.len == 1);
    /* A cursor walks across chunk ends from any row, marks or not. */
    for (uint64_t start = 0; start < 4137; start += 997) {
        result_cursor cur;
        assert(result_seek(&cur, &r, start));
        for (uint64_t row = start; row < start + 300 && row < 4137; row++) {
            if (row > start) {
                assert(result_next_row(&cur));
            }
            assert(result_next_cell(&cur, &c));
            char buf[24] = {0};
            memcpy(buf, c.bytes.ptr, c.bytes.len);
            assert(strtoull(buf, NULL, 10) == row);
        }
    }
    result_cursor end;
    assert(result_seek(&end, &r, 4136) && !result_next_row(&end));
}

static void order_and_quota(void) {
    result r;
    assert(sc_arena_reset(&book) && result_init(&r, &book, 2));
    chunk(1, 0, 10, 1);
    assert(result_add_chunk(&r, &pool[1], 1, 0) == RESULT_BAD); /* before COLUMNS */
    columns(0, 1, NULL, NULL);
    assert(result_set_columns(&r, &pool[0], 0) == RESULT_ADDED);
    assert(result_set_columns(&r, &pool[0], 0) == RESULT_BAD);
    chunk(2, 10, 5, 1);
    assert(result_add_chunk(&r, &pool[2], 2, 10) == RESULT_BAD); /* rows 0..9 missing */
    assert(result_add_chunk(&r, &pool[1], 1, 0) == RESULT_ADDED);
    assert(result_add_chunk(&r, &pool[1], 1, 0) == RESULT_BAD); /* behind */
    assert(result_add_chunk(&r, &pool[2], 2, 10) == RESULT_ADDED);
    chunk(3, 15, 1, 1);
    assert(result_add_chunk(&r, &pool[3], 3, 15) == RESULT_FULL);
    assert(r.rows == 15 && r.count == 2);
    /* A chunk whose cells do not match the column count is refused. */
    result r2;
    assert(result_init(&r2, &book, 2));
    columns(4, 2, NULL, NULL);
    assert(result_set_columns(&r2, &pool[4], 4) == RESULT_ADDED);
    chunk(5, 0, 3, 1);
    assert(result_add_chunk(&r2, &pool[5], 5, 0) == RESULT_BAD);
    chunk(5, 0, 3, 3);
    assert(result_add_chunk(&r2, &pool[5], 5, 0) == RESULT_BAD);
    assert(result_set_columns(&r2, &pool[5], 5) == RESULT_BAD); /* not COLUMNS */
}

static void null_and_empty(void) {
    result r;
    assert(sc_arena_reset(&book) && result_init(&r, &book, 1));
    columns(0, 2, NULL, NULL);
    assert(result_set_columns(&r, &pool[0], 0) == RESULT_ADDED);
    sc_arena *a = fresh(1);
    proto_writer w;
    proto_header h = {PROTO_CHUNK, 1, 1, 1, 0, 0};
    sc_str none = {NULL, 0, a->epoch};
    assert(proto_begin(&w, a, h) && proto_put_u64(&w, PROTO_CELL_NULL) &&
           proto_put_str(&w, a, none) && proto_put_u64(&w, 0) && proto_put_str(&w, a, none) &&
           proto_end(&w));
    assert(result_add_chunk(&r, a, 1, 0) == RESULT_ADDED);
    proto_cell n, e;
    assert(result_cell(&r, 0, 0, &n) && result_cell(&r, 0, 1, &e));
    assert(n.null && !e.null && n.len == 0 && e.len == 0 && e.bytes.len == 0);
}

static void close_returns_each_once(void) {
    result r;
    assert(sc_arena_reset(&book) && result_init(&r, &book, 4));
    columns(6, 1, NULL, NULL);
    assert(result_set_columns(&r, &pool[6], 6) == RESULT_ADDED);
    chunk(2, 0, 3, 1);
    chunk(5, 3, 3, 1);
    chunk(1, 6, 3, 1);
    assert(result_add_chunk(&r, &pool[2], 2, 0) == RESULT_ADDED &&
           result_add_chunk(&r, &pool[5], 5, 3) == RESULT_ADDED &&
           result_add_chunk(&r, &pool[1], 1, 6) == RESULT_ADDED);
    assert(sc_arena_reset(&oa));
    sc_tslice out = SC_TSLICE_ALLOC(size_t, &oa, 8);
    assert(result_close(&r, &oa, out) == 4);
    unsigned seen[ARENAS] = {0};
    for (size_t i = 0; i < 4; i++) {
        size_t v = 0;
        assert(SC_TSLICE_GET(size_t, &oa, out, i, &v) && v < ARENAS);
        seen[v]++;
    }
    assert(seen[6] == 1 && seen[2] == 1 && seen[5] == 1 && seen[1] == 1);
    assert(r.rows == 0 && r.count == 0 && !r.has_columns);
    assert(result_close(&r, &oa, out) == 0);
    /* The closed result takes a new set. */
    assert(result_set_columns(&r, &pool[6], 6) == RESULT_ADDED);
}

/* Width by bytes: 7 points a byte; NULL measures as 28. */
static uint32_t by_bytes(void *ctx, const sc_arena *a, sc_str s, bool null) {
    (void)ctx;
    (void)a;
    return null ? 28u : (uint32_t)(7u * s.len);
}

static void widths(void) {
    result r;
    assert(sc_arena_reset(&book) && result_init(&r, &book, 2));
    const char *names[] = {"a_long_header_name", "v"};
    const uint32_t oids[] = {25, 23};
    columns(0, 2, names, oids);
    assert(result_set_columns(&r, &pool[0], 0) == RESULT_ADDED);
    /* Row 0: "x", 4 KiB. Row 1: "y", "1". The second chunk's wide value is
     * never read. */
    sc_arena *a = fresh(1);
    alignas(8) static uint8_t big_mem[8192];
    sc_arena big;
    sc_arena_init(&big, big_mem, sizeof big_mem);
    uint8_t *raw = big_mem + 4096;
    memset(raw, 'w', 4096);
    sc_str wide = sc_str_from_lit(&big, raw, 4096);
    proto_writer w;
    proto_header h = {PROTO_CHUNK, 1, 1, 2, 0, 0};
    assert(proto_begin(&w, a, h) && proto_put_u64(&w, 1) && proto_put_str(&w, &big, lit(&big, "x")) &&
           proto_put_u64(&w, 4096) && proto_put_str(&w, &big, wide) && proto_put_u64(&w, 1) &&
           proto_put_str(&w, &big, lit(&big, "y")) && proto_put_u64(&w, 1) &&
           proto_put_str(&w, &big, lit(&big, "1")) && proto_end(&w));
    assert(result_add_chunk(&r, a, 1, 0) == RESULT_ADDED);
    sc_arena *b = fresh(2);
    h.arg = 1;
    assert(proto_begin(&w, b, h) && proto_put_u64(&w, 4096) && proto_put_str(&w, &big, wide) &&
           proto_put_u64(&w, 1) && proto_put_str(&w, &big, lit(&big, "2")) && proto_end(&w));
    assert(result_add_chunk(&r, b, 2, 2) == RESULT_ADDED);
    assert(sc_arena_reset(&oa));
    sc_tslice ws = SC_TSLICE_ALLOC(uint32_t, &oa, 2);
    assert(result_widths(&r, &oa, ws, 2, by_bytes, NULL, 12, 300));
    uint32_t w0 = 0, w1 = 0;
    assert(SC_TSLICE_GET(uint32_t, &oa, ws, 0, &w0) && SC_TSLICE_GET(uint32_t, &oa, ws, 1, &w1));
    assert(w0 == 18u * 7u + 12u); /* the header is wider than "x" and "y" */
    assert(w1 == 300u);           /* the 4 KiB value hits the maximum exactly */
    assert(result_numeric(23) && result_numeric(1700) && result_numeric(701) &&
           !result_numeric(25) && !result_numeric(16) && !result_numeric(1082));
    sc_str name;
    uint32_t oid = 0;
    assert(result_column(&r, 1, &name, &oid) && oid == 23 && name.len == 1);
    assert(!result_column(&r, 2, &name, &oid));
    /* A stale chunk arena fails the read rather than reading reset memory. */
    assert(sc_arena_reset(&pool[1]));
    proto_cell c;
    assert(!result_cell(&r, 0, 0, &c));
}

int main(void) {
    sc_arena_init(&book, book_mem, sizeof book_mem);
    sc_arena_init(&oa, out_mem, sizeof out_mem);
    assert(result_book_bytes(4) < sizeof book_mem);
    lookup();
    order_and_quota();
    null_and_empty();
    close_returns_each_once();
    widths();
    puts("explorer_result_test: ok");
    return 0;
}
