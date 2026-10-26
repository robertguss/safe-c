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
    alignas(8) static uint8_t tmp_mem[1u << 15];
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
    assert(result_add_chunk(&r, &pool[1], 1) == RESULT_ADDED);
    assert(result_add_chunk(&r, &pool[2], 2) == RESULT_ADDED);
    assert(result_add_chunk(&r, &pool[3], 3) == RESULT_ADDED);
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
    assert(result_cell(&r, 5, 1, &c) && c.bytes.len == 1 && c.bytes.ptr[0] == 'x');
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
    assert(result_add_chunk(&r, &pool[1], 1) == RESULT_BAD); /* before COLUMNS */
    columns(0, 1, NULL, NULL);
    assert(result_set_columns(&r, &pool[0], 0) == RESULT_ADDED);
    assert(result_set_columns(&r, &pool[0], 0) == RESULT_BAD);
    chunk(2, 10, 5, 1);
    assert(result_add_chunk(&r, &pool[1], 1) == RESULT_ADDED);
    assert(result_add_chunk(&r, &pool[2], 2) == RESULT_ADDED);
    assert(first_cell(&r, 10) == 10); /* each chunk follows the rows held */
    chunk(3, 15, 1, 1);
    assert(result_add_chunk(&r, &pool[3], 3) == RESULT_FULL);
    assert(r.rows == 15 && r.count == 2);
    /* A chunk whose cells do not match the column count is refused. */
    result r2;
    assert(result_init(&r2, &book, 2));
    columns(4, 2, NULL, NULL);
    assert(result_set_columns(&r2, &pool[4], 4) == RESULT_ADDED);
    chunk(5, 0, 3, 1);
    assert(result_add_chunk(&r2, &pool[5], 5) == RESULT_BAD);
    chunk(5, 0, 3, 3);
    assert(result_add_chunk(&r2, &pool[5], 5) == RESULT_BAD);
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
    assert(result_add_chunk(&r, a, 1) == RESULT_ADDED);
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
    assert(result_add_chunk(&r, &pool[2], 2) == RESULT_ADDED &&
           result_add_chunk(&r, &pool[5], 5) == RESULT_ADDED &&
           result_add_chunk(&r, &pool[1], 1) == RESULT_ADDED);
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

/* A CHUNK in arena i: `rows` x `cols` cells from `v`, row-major. A NULL
 * pointer is SQL NULL; "WIDE" is a 4 KiB value; "e<n>" is n bytes of 'e'. */
static void cells(size_t i, uint64_t rows, uint64_t cols, const char *const *v) {
    sc_arena *a = fresh(i);
    alignas(8) static uint8_t tmp_mem[8192];
    static uint8_t raw[4096];
    sc_arena tmp;
    proto_writer w;
    proto_header h = {PROTO_CHUNK, 1, 1, rows, 0, 0};
    assert(proto_begin(&w, a, h));
    for (uint64_t k = 0; k < rows * cols; k++) {
        sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
        sc_str none = {NULL, 0, tmp.epoch};
        if (v[k] == NULL) {
            assert(proto_put_u64(&w, PROTO_CELL_NULL) && proto_put_str(&w, &tmp, none));
            continue;
        }
        size_t n = strlen(v[k]);
        const uint8_t *src = (const uint8_t *)v[k];
        if (strcmp(v[k], "WIDE") == 0 || v[k][0] == 'e') {
            n = v[k][0] == 'e' ? (size_t)atoi(v[k] + 1) : 4096u;
            memset(raw, v[k][0] == 'e' ? 'e' : 'w', n);
            src = raw;
        }
        sc_str s = n == 0 ? none : sc_str_from_lit(&tmp, src, n);
        assert(proto_put_u64(&w, n) && proto_put_str(&w, &tmp, s));
    }
    assert(proto_end(&w));
}

/* Headers measure 7 points a byte, cells 6, a NULL 28. */
static uint32_t by_bytes(void *ctx, const sc_arena *a, sc_str s, result_text kind) {
    (void)ctx;
    (void)a;
    return kind == RESULT_TEXT_NULL     ? 28u
           : kind == RESULT_TEXT_HEADER ? (uint32_t)(7u * s.len)
                                        : (uint32_t)(6u * s.len);
}

static void widths(void) {
    result r;
    assert(sc_arena_reset(&book) && result_init(&r, &book, 2));
    const char *names[] = {"a_long_header_name", "v", "n", "edge", "f"};
    const uint32_t oids[] = {25, 23, 25, 25, 25};
    columns(0, 5, names, oids);
    assert(result_set_columns(&r, &pool[0], 0) == RESULT_ADDED);
    /* Three rows are measured; the fourth and the second chunk are not. */
    const char *first[] = {
        "x",    "1",    NULL, "e4",  "e1",  /* */
        "y",    "2",    "",   "e49", "e47", /* */
        "z",    "WIDE", NULL, "e1",  "e2",  /* the 4 KiB value is in the last measured row */
        "WIDE", "WIDE", "e9", "e1",  "e1",  /* not measured */
    };
    cells(1, 4, 5, first);
    assert(result_add_chunk(&r, &pool[1], 1) == RESULT_ADDED);
    const char *second[] = {"WIDE", "WIDE", "WIDE", "WIDE", "WIDE"};
    cells(2, 1, 5, second);
    assert(result_add_chunk(&r, &pool[2], 2) == RESULT_ADDED);
    assert(sc_arena_reset(&oa));
    sc_tslice ws = SC_TSLICE_ALLOC(uint32_t, &oa, 5);
    result_width_rule rule = {by_bytes, NULL, 3, 12, 300};
    assert(result_widths(&r, &oa, ws, 5, rule));
    uint32_t got[5];
    for (size_t c = 0; c < 5; c++) {
        assert(SC_TSLICE_GET(uint32_t, &oa, ws, c, &got[c]));
    }
    assert(got[0] == 18u * 7u + 12u); /* the header, in the header measure, beats every cell read */
    assert(got[1] == 300u);           /* the 4 KiB value hits the maximum exactly */
    assert(got[2] == 28u + 12u);      /* a NULL measures as "NULL", not as empty */
    assert(got[3] == 300u);           /* 294 + 12 would pass the maximum */
    assert(got[4] == 47u * 6u + 12u); /* just under the maximum */
    rule.pad = 301;
    assert(!result_widths(&r, &oa, ws, 5, rule));
    assert(result_numeric(20) && result_numeric(21) && result_numeric(23) && result_numeric(26) &&
           result_numeric(700) && result_numeric(701) && result_numeric(1700));
    assert(!result_numeric(25) && !result_numeric(16) && !result_numeric(1082) &&
           !result_numeric(1043) && !result_numeric(0));
    sc_str name;
    uint32_t oid = 0;
    assert(result_column(&r, 1, &name, &oid) && oid == 23 && name.len == 1);
    assert(!result_column(&r, 5, &name, &oid));
    /* A stale chunk arena fails the read rather than reading reset memory. */
    assert(sc_arena_reset(&pool[1]));
    proto_cell c;
    assert(!result_cell(&r, 0, 0, &c));
}

/* Each refusal clause of result_set_columns and result_add_chunk. */
static void malformed(void) {
    result r;
    assert(sc_arena_reset(&book) && result_init(&r, &book, 4));
    /* A zero-cell chunk before COLUMNS would walk no cells: still refused. */
    cells(1, 3, 0, NULL);
    assert(result_add_chunk(&r, &pool[1], 1) == RESULT_BAD);
    /* COLUMNS with a trailing word. */
    sc_arena *a = fresh(0);
    alignas(8) static uint8_t tmp_mem[64];
    sc_arena tmp;
    sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
    proto_writer w;
    proto_header h = {PROTO_COLUMNS, 1, 1, 0, 0, 0};
    assert(proto_begin(&w, a, h) && proto_put_u64(&w, 1) &&
           proto_put_str(&w, &tmp, lit(&tmp, "a")) && proto_put_u64(&w, 23) &&
           proto_put_u64(&w, 7) && proto_end(&w));
    assert(result_set_columns(&r, a, 0) == RESULT_BAD);
    /* An oid past 32 bits. */
    a = fresh(0);
    assert(proto_begin(&w, a, h) && proto_put_u64(&w, 1) &&
           proto_put_str(&w, &tmp, lit(&tmp, "a")) && proto_put_u64(&w, UINT64_C(1) << 32) &&
           proto_end(&w));
    assert(result_set_columns(&r, a, 0) == RESULT_BAD);
    /* Postgres's limit of 1,664 columns, and one more. */
    columns(0, RESULT_MAX_COLUMNS + 1u, NULL, NULL);
    assert(result_set_columns(&r, &pool[0], 0) == RESULT_BAD);
    columns(0, RESULT_MAX_COLUMNS, NULL, NULL);
    assert(result_set_columns(&r, &pool[0], 0) == RESULT_ADDED);
    sc_tslice none = {{NULL, 0, 0}, 0};
    assert(result_close(&r, NULL, none) == 1);
    /* A chunk of zero rows. */
    columns(0, 1, NULL, NULL);
    assert(result_set_columns(&r, &pool[0], 0) == RESULT_ADDED);
    cells(1, 0, 1, NULL);
    assert(result_add_chunk(&r, &pool[1], 1) == RESULT_BAD);
    assert(r.count == 0 && r.rows == 0);
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
    malformed();
    puts("explorer_result_test: ok");
    return 0;
}
