/* Tests the explorer's table pages (examples/explorer/page.c, plan U20): the
 * Content page SQL and its ORDER BY for each kind of relation, quoting, the
 * count read on the metadata link, the pager's and the row count's text,
 * when ‹ and › are enabled, and the Structure view's rows and chips. The
 * catalog is filled with results built by hand, as the worker writes them.
 * Core code, built in. */
#include "../examples/explorer/proto.c"
#include "../examples/explorer/result.c"
#include "../examples/explorer/catalog.c"
#include "../examples/explorer/catalog_sql.c"
#include "../examples/explorer/catalog_duckdb.c"
#include "../examples/explorer/catalog_sqlite.c"
#include "../examples/explorer/page.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

#define ARENA_BYTES (256u * 1024u)

static alignas(8) uint8_t pool_mem[2][ARENA_BYTES];
static alignas(8) uint8_t book_mem[1u << 18];
static alignas(8) uint8_t rec_mem[1u << 20];
static alignas(8) uint8_t keep_mem[1u << 14];
static alignas(8) uint8_t tmp_mem[1u << 14];
static alignas(8) uint8_t defs_mem[1u << 12];
static alignas(8) uint8_t out_mem[1u << 14];
static sc_arena pool[2], book, rec, keep, tmp, defs, out_a;

static sc_str lit(sc_arena *a, const char *s) {
    size_t n = strlen(s);
    return n == 0 ? (sc_str){NULL, 0, a->epoch} : sc_str_from_lit(a, (const uint8_t *)s, n);
}

static bool same(const sc_arena *a, sc_str s, const char *want) {
    return sc_str_eq_lit(a, s, (const uint8_t *)want, strlen(want));
}

static result build(uint64_t cols, uint64_t rows, const char *const *cells) {
    result r;
    assert(sc_arena_reset(&book) && result_init(&r, &book, 4));
    if (cols == 0) {
        return r;
    }
    sc_arena_init(&pool[0], pool_mem[0], ARENA_BYTES);
    proto_writer w;
    proto_header h = {PROTO_COLUMNS, 1, 1, 0, 0, 0};
    assert(proto_begin(&w, &pool[0], h) && proto_put_u64(&w, cols));
    for (uint64_t c = 0; c < cols; c++) {
        assert(sc_arena_reset(&tmp) && proto_put_str(&w, &tmp, lit(&tmp, "c")) && proto_put_u64(&w, 25u));
    }
    assert(proto_end(&w) && result_set_columns(&r, &pool[0], 0) == RESULT_ADDED);
    if (rows == 0) {
        return r;
    }
    sc_arena_init(&pool[1], pool_mem[1], ARENA_BYTES);
    h.kind = PROTO_CHUNK;
    h.arg = rows;
    assert(proto_begin(&w, &pool[1], h));
    for (uint64_t k = 0; k < rows * cols; k++) {
        const char *s = cells[k];
        assert(sc_arena_reset(&tmp));
        assert(proto_put_u64(&w, s == NULL ? PROTO_CELL_NULL : strlen(s)) &&
               proto_put_str(&w, &tmp, lit(&tmp, s == NULL ? "" : s)));
    }
    assert(proto_end(&w) && result_add_chunk(&r, &pool[1], 1) == RESULT_ADDED);
    return r;
}

static void take(catalog *c, catalog_read_kind kind, uint64_t cols, uint64_t rows, const char *const *cells) {
    catalog_read rd;
    assert(catalog_next(c, &rd) && rd.kind == kind);
    result r = build(cols, rows, cells);
    assert(catalog_take(c, rd, &r));
}

#define S 100u
#define MY 101u

static char quotes[64];

static void loaded(catalog *c) {
    static const char *const session[] = {"150019", "15.19", "on", "app", "robert", "localhost"};
    static const char *const schemas[] = {"101", "My Schema", "100", "s"};
    static const char *const rels[] = {"1", "t", "r", "2400", "2", "nokey", "r", "-1",
                                       "3", "parts", "p", "-1", "4", "v", "v", "-1",
                                       "5", "mv", "m", "0", "6", "ft", "f", "-1",
                                       "7", "wide", "r", "-1"};
    static const char *const mine[] = {"8", "select", "r", "-1", "9", quotes, "r", "-1"};
    memset(quotes, '"', 63);
    assert(sc_arena_reset(&rec) && sc_arena_reset(&keep) && catalog_init(c, &rec, &keep, &defs));
    assert(catalog_restart(c));
    take(c, CATALOG_READ_TIMEOUT, 0, 0, NULL);
    take(c, CATALOG_READ_SESSION, 6, 1, session);
    take(c, CATALOG_READ_SCHEMAS, 2, 2, schemas);
    assert(catalog_expand(c, 0) && catalog_expand(c, 1));
    take(c, CATALOG_READ_RELATIONS, 4, 2, mine);
    take(c, CATALOG_READ_FUNCTIONS, 5, 0, NULL);
    take(c, CATALOG_READ_RELATIONS, 4, 7, rels);
    take(c, CATALOG_READ_FUNCTIONS, 5, 0, NULL);
}

static void detail(catalog *c, uint32_t schema, uint32_t oid, uint64_t ncols, const char *const *cols,
                   uint64_t ncons, const char *const *cons, uint64_t nidx, const char *const *idx) {
    assert(catalog_want_detail(c, schema, oid));
    take(c, CATALOG_READ_COLUMNS, 8, ncols, cols);
    take(c, CATALOG_READ_CONSTRAINTS, 5, ncons, cons);
    take(c, CATALOG_READ_INDEXES, 4, nidx, idx);
}

static const char *const t_cols[] = {
    "1", "b", "integer", "t", NULL, "", "", "2",
    "2", "a", "bigint", "t", NULL, "a", "", "1",
    "3", "owner", "integer", "t", NULL, "", "", "0",
    "4", "code", "text", "f", "'x'::text", "", "", "0",
    "5", "total", "numeric", "f", "(a * 2)", "", "s", "0"};
static const char *const t_cons[] = {
    "t_code_key", "u", "UNIQUE (code)", "4", NULL,
    "t_owner_fkey", "f", "FOREIGN KEY (owner) REFERENCES people(id)", "3", "people",
    "t_pkey", "p", "PRIMARY KEY (a, b)", "2 1", NULL,
    "t_total_check", "c", "CHECK (total > 0)", "5", NULL,
    "t_pair_key", "u", "UNIQUE (a, owner)", "2 3", NULL};
static const char *const t_idx[] = {"t_pkey", "t", "t", "CREATE UNIQUE INDEX t_pkey ON s.t USING btree (a, b)",
                                    "t_code_key", "f", "t", "CREATE UNIQUE INDEX t_code_key ON s.t USING btree (code)"};
static const char *const one_col[] = {"1", "n", "integer", "f", NULL, "", "", "0"};
static const char *const select_cols[] = {"1", "from", "integer", "t", NULL, "", "", "1",
                                          "2", "a\"b", "text", "f", NULL, "", "", "0"};

static bool page_is(const catalog *c, uint32_t schema, uint32_t oid, uint64_t page, const char *want,
                    bool note) {
    sc_str s;
    bool got_note = !note;
    assert(sc_arena_reset(&out_a));
    return page_sql(c, schema, oid, page, PAGE_SIZE, &out_a, &s, &got_note) && got_note == note && same(&out_a, s, want);
}

static void page_queries(void) {
    catalog c;
    loaded(&c);
    assert(!page_ready(&c, S, 1) && page_ready(&c, S, 4) && page_ready(&c, S, 6));
    assert(!page_ready(&c, S, 99));
    detail(&c, S, 1, 5, t_cols, 5, t_cons, 2, t_idx);
    assert(page_ready(&c, S, 1));
    assert(page_is(&c, S, 1, 3, "SELECT * FROM \"s\".\"t\" ORDER BY \"a\", \"b\" LIMIT 1000 OFFSET 2000", false));
    sc_str sized;
    bool sized_note = true;
    assert(sc_arena_reset(&out_a) && page_sql(&c, S, 1, 3, 250, &out_a, &sized, &sized_note));
    assert(!sized_note && same(&out_a, sized, "SELECT * FROM \"s\".\"t\" ORDER BY \"a\", \"b\" LIMIT 250 OFFSET 500"));
    assert(!page_sql(&c, S, 1, 3, 99, &out_a, &sized, &sized_note));
    assert(page_is(&c, S, 1, 1, "SELECT * FROM \"s\".\"t\" ORDER BY \"a\", \"b\" LIMIT 1000 OFFSET 0", false));
    detail(&c, S, 2, 1, one_col, 0, NULL, 0, NULL);
    assert(page_is(&c, S, 2, 2, "SELECT * FROM \"s\".\"nokey\" ORDER BY ctid LIMIT 1000 OFFSET 1000", false));
    detail(&c, S, 3, 1, one_col, 0, NULL, 0, NULL);
    assert(page_is(&c, S, 3, 1, "SELECT * FROM \"s\".\"parts\" ORDER BY tableoid, ctid LIMIT 1000 OFFSET 0", false));
    detail(&c, S, 5, 1, one_col, 0, NULL, 0, NULL);
    assert(page_is(&c, S, 5, 1, "SELECT * FROM \"s\".\"mv\" ORDER BY ctid LIMIT 1000 OFFSET 0", false));
    assert(page_is(&c, S, 4, 2, "SELECT * FROM \"s\".\"v\" LIMIT 1000 OFFSET 1000", true));
    assert(page_is(&c, S, 6, 1, "SELECT * FROM \"s\".\"ft\" LIMIT 1000 OFFSET 0", true));
    detail(&c, MY, 8, 2, select_cols, 0, NULL, 0, NULL);
    assert(page_is(&c, MY, 8, 1,
                   "SELECT * FROM \"My Schema\".\"select\" ORDER BY \"from\" LIMIT 1000 OFFSET 0", false));
    sc_str s;
    assert(sc_arena_reset(&out_a));
    assert(page_sql(&c, MY, 8, 2, PAGE_SIZE, &out_a, &s, &(bool){true}));
    sc_str kept = s;
    bool note = false;
    assert(!page_sql(&c, S, 1, 0, PAGE_SIZE, &out_a, &s, &note) && !page_sql(&c, S, 99, 1, PAGE_SIZE, &out_a, &s, &note));
    assert(s.ptr == kept.ptr && s.len == kept.len && !note);
    assert(catalog_want_detail(&c, S, 7));
    catalog_read rd;
    for (int i = 0; i < 3; i++) {
        assert(catalog_next(&c, &rd));
        catalog_fail(&c, rd, &tmp, lit(&tmp, "canceling statement due to statement timeout"));
    }
    assert(page_ready(&c, S, 7) && page_is(&c, S, 7, 1, "SELECT * FROM \"s\".\"wide\" ORDER BY ctid LIMIT 1000 OFFSET 0", false));
}

static void page_quoting(void) {
    catalog c;
    loaded(&c);
    char longest[160];
    memset(longest, '"', 128);
    longest[128] = 0;
    const char *const cols[] = {"1", quotes, "integer", "t", NULL, "", "", "2",
                                "2", "a\"b", "text", "t", NULL, "", "", "1"};
    detail(&c, MY, 9, 2, cols, 0, NULL, 0, NULL);
    char want[512];
    (void)snprintf(want, sizeof want,
                   "SELECT * FROM \"My Schema\".%s ORDER BY \"a\"\"b\", %s LIMIT 1000 OFFSET 1000", longest,
                   longest);
    assert(page_is(&c, MY, 9, 2, want, false));
}

static catalog_relation rel(const catalog *c, uint32_t schema, uint32_t oid) {
    size_t si = 0, ri = 0;
    catalog_schema s;
    catalog_relation r;
    assert(catalog_find_relation(c, schema, oid, &si, &ri) && catalog_schema_at(c, si, &s) &&
           catalog_relation_at(c, &s, ri, &r));
    return r;
}

static void counts(void) {
    catalog c;
    loaded(&c);
    catalog_read rd;
    assert(catalog_want_count(&c, S, 1));
    assert(rel(&c, S, 1).count_load == CATALOG_LOAD_PENDING);
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_COUNT_TIMEOUT && rd.oid == 1);
    assert(sc_arena_reset(&out_a));
    sc_str sql;
    assert(catalog_sql(&c, rd, &out_a, &sql) && same(&out_a, sql, "SET statement_timeout = '2s'"));
    result r = build(0, 0, NULL);
    assert(catalog_take(&c, rd, &r));
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_COUNT && rd.oid == 1 && rd.schema == S);
    static const char *const n[] = {"2500"};
    r = build(1, 1, n);
    assert(catalog_take(&c, rd, &r));
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_TIMEOUT);
    assert(catalog_sql(&c, rd, &out_a, &sql) && same(&out_a, sql, "SET statement_timeout = '10s'"));
    r = build(0, 0, NULL);
    assert(catalog_take(&c, rd, &r));
    catalog_relation t = rel(&c, S, 1);
    assert(t.count_load == CATALOG_LOAD_DONE && t.count == 2500);

    assert(catalog_want_count(&c, S, 2));
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_COUNT_TIMEOUT);
    r = build(0, 0, NULL);
    assert(catalog_take(&c, rd, &r));
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_COUNT);
    catalog_fail(&c, rd, &tmp, lit(&tmp, "canceling statement due to statement timeout"));
    assert(rel(&c, S, 2).count_load == CATALOG_LOAD_FAILED && c.schemas_load == CATALOG_LOAD_DONE);
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_TIMEOUT);
    assert(catalog_want_count(&c, S, 3));
    assert(catalog_next(&c, &rd) && catalog_next(&c, &rd) && rd.kind == CATALOG_READ_COUNT);
    static const char *const bad[] = {"many"};
    r = build(1, 1, bad);
    assert(!catalog_take(&c, rd, &r) && rel(&c, S, 3).count_load == CATALOG_LOAD_FAILED);
    assert(!catalog_want_count(&c, S, 99));

    page_total pt = page_total_open(&t);
    assert(pt.estimate_known && pt.estimate == 2400 && pt.count_state == PAGE_COUNT_PENDING);
    pt = page_total_update(pt, &c, S, 1);
    assert(pt.count_state == PAGE_COUNT_KNOWN && pt.count == 2500);
    catalog_relation nk = rel(&c, S, 2);
    pt = page_total_open(&nk);
    assert(!pt.estimate_known);
    pt = page_total_update(pt, &c, S, 2);
    assert(pt.count_state == PAGE_COUNT_TIMED_OUT);
    assert(catalog_want_count(&c, S, 2));
    assert(page_total_update(pt, &c, S, 2).count_state == PAGE_COUNT_TIMED_OUT);
}

static bool label_is(uint64_t page, page_total t, const char *want) {
    bool lost = false;
    assert(sc_arena_reset(&out_a));
    sc_str s = page_label(&out_a, page, t, &lost);
    return !lost && same(&out_a, s, want);
}

static bool rows_are(uint64_t page, uint64_t rows, page_total t, bool capped, const char *want) {
    bool lost = false;
    assert(sc_arena_reset(&out_a));
    sc_str s = page_rows_label(&out_a, page, rows, t, capped, &lost);
    return !lost && same(&out_a, s, want);
}

static void pager(void) {
    page_total unknown = {false, 0, PAGE_COUNT_PENDING, 0, PAGE_SIZE};
    assert(label_is(1, unknown, "Page 1"));
    page_total counted = unknown;
    counted.count_state = PAGE_COUNT_KNOWN;
    counted.count = 2500;
    assert(label_is(1, counted, "Page 1 of 3"));
    page_total est = {true, 2400, PAGE_COUNT_PENDING, 0, PAGE_SIZE};
    assert(label_is(1, est, "Page 1 of ~3"));
    page_total timed = est;
    timed.count_state = PAGE_COUNT_TIMED_OUT;
    assert(label_is(2, timed, "Page 2"));
    page_total empty = {true, 0, PAGE_COUNT_PENDING, 0, PAGE_SIZE};
    assert(label_is(1, empty, "Page 1 of ~1"));
    page_total exact = counted;
    exact.count = 3000;
    assert(label_is(3, exact, "Page 3 of 3"));

    page_total about = {true, 2500, PAGE_COUNT_PENDING, 0, PAGE_SIZE};
    assert(rows_are(3, 500, about, false, "Rows 2,001\xE2\x80\x93" "2,500 of ~2,500"));
    assert(rows_are(3, 500, counted, false, "Rows 2,001\xE2\x80\x93" "2,500 of 2,500"));
    assert(rows_are(1, 1000, unknown, false, "Rows 1\xE2\x80\x93" "1,000"));
    assert(rows_are(1, 812, unknown, true, "Rows 1\xE2\x80\x93" "812 (memory limit)"));
    assert(rows_are(4, 0, counted, false, "No rows on this page"));

    assert(!page_can_prev(1) && page_can_prev(2));
    assert(page_can_next(true, PAGE_SIZE, false, PAGE_SIZE));
    assert(!page_can_next(true, 999, false, PAGE_SIZE));
    assert(!page_can_next(true, 812, true, PAGE_SIZE) && !page_can_next(true, PAGE_SIZE, true, PAGE_SIZE));
    assert(!page_can_next(false, PAGE_SIZE, false, PAGE_SIZE));
    assert(!page_can_next(true, 0, false, PAGE_SIZE));
    about.size = 100;
    assert(label_is(3, about, "Page 3 of ~25"));
    assert(rows_are(3, 100, about, false, "Rows 201\xE2\x80\x93" "300 of ~2,500"));
    assert(page_can_next(true, 100, false, 100) && !page_can_next(true, 99, false, 100));
}

static page_row row_at(const catalog *c, const catalog_relation *r, size_t i) {
    page_row p;
    assert(page_structure_row(c, r, i, &out_a, &p));
    return p;
}

static void structure(void) {
    catalog c;
    loaded(&c);
    detail(&c, S, 1, 5, t_cols, 5, t_cons, 2, t_idx);
    catalog_relation t = rel(&c, S, 1);
    assert(sc_arena_reset(&out_a));
    assert(page_structure_count(&c, &t) == 12);
    page_row p = row_at(&c, &t, 0);
    assert(p.kind == PAGE_ROW_SECTION && same(&out_a, p.name, "Columns"));
    p = row_at(&c, &t, 1);
    assert(p.kind == PAGE_ROW_COLUMN && same(&rec, p.name, "b") && same(&rec, p.type, "integer"));
    assert(p.chips == (PAGE_CHIP_PRIMARY_KEY | PAGE_CHIP_NOT_NULL));
    p = row_at(&c, &t, 2);
    assert(p.chips == (PAGE_CHIP_PRIMARY_KEY | PAGE_CHIP_NOT_NULL | PAGE_CHIP_IDENTITY));
    p = row_at(&c, &t, 3);
    assert(same(&rec, p.name, "owner") && p.chips == (PAGE_CHIP_NOT_NULL | PAGE_CHIP_FOREIGN));
    assert(same(&rec, p.target, "people"));
    p = row_at(&c, &t, 4);
    assert(same(&rec, p.name, "code") && p.chips == PAGE_CHIP_UNIQUE && same(&rec, p.detail, "'x'::text"));
    p = row_at(&c, &t, 5);
    assert(same(&rec, p.name, "total") && p.chips == PAGE_CHIP_GENERATED && same(&rec, p.detail, "(a * 2)"));
    p = row_at(&c, &t, 6);
    assert(p.kind == PAGE_ROW_SECTION && same(&out_a, p.name, "Indexes"));
    p = row_at(&c, &t, 7);
    assert(p.kind == PAGE_ROW_INDEX && same(&rec, p.name, "t_pkey"));
    assert(same(&rec, p.detail, "CREATE UNIQUE INDEX t_pkey ON s.t USING btree (a, b)"));
    p = row_at(&c, &t, 9);
    assert(p.kind == PAGE_ROW_SECTION && same(&out_a, p.name, "Constraints"));
    p = row_at(&c, &t, 10);
    assert(p.kind == PAGE_ROW_CONSTRAINT && same(&rec, p.name, "t_owner_fkey"));
    p = row_at(&c, &t, 11);
    assert(same(&rec, p.name, "t_total_check") && same(&rec, p.detail, "CHECK (total > 0)"));
    assert(!page_structure_row(&c, &t, 12, &out_a, &p));

    static char names[200][8];
    static const char *wide[200 * 8];
    for (size_t i = 0; i < 200; i++) {
        snprintf(names[i], sizeof names[i], "%zu", i + 1);
        const char *row[] = {names[i], names[i], "integer", "f", NULL, "", "", "0"};
        memcpy(&wide[i * 8], row, sizeof row);
    }
    detail(&c, S, 7, 200, wide, 0, NULL, 0, NULL);
    catalog_relation w = rel(&c, S, 7);
    assert(page_structure_count(&c, &w) == 203);
    p = row_at(&c, &w, 200);
    assert(p.kind == PAGE_ROW_COLUMN && same(&rec, p.name, "200"));
    assert(row_at(&c, &w, 201).kind == PAGE_ROW_SECTION && row_at(&c, &w, 202).kind == PAGE_ROW_SECTION);
}

static void count_timeout_fails(void) {
    catalog c;
    loaded(&c);
    catalog_read rd;
    assert(catalog_want_count(&c, S, 1));
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_COUNT_TIMEOUT);
    catalog_fail(&c, rd, &tmp, lit(&tmp, "permission denied to set parameter"));
    assert(rel(&c, S, 1).count_load == CATALOG_LOAD_FAILED && c.schemas_load == CATALOG_LOAD_DONE);
    catalog_relation t = rel(&c, S, 1);
    page_total pt = page_total_update(page_total_open(&t), &c, S, 1);
    assert(pt.count_state == PAGE_COUNT_TIMED_OUT && label_is(4, pt, "Page 4"));
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_TIMEOUT);
    assert(!catalog_next(&c, &rd));
}

static void open_order(void) {
    catalog c;
    loaded(&c);
    page_open(&c, S, 1);
    const catalog_read_kind order[] = {CATALOG_READ_COLUMNS, CATALOG_READ_CONSTRAINTS, CATALOG_READ_INDEXES,
                                       CATALOG_READ_COUNT_TIMEOUT, CATALOG_READ_COUNT, CATALOG_READ_TIMEOUT};
    catalog_read rd;
    for (size_t i = 0; i < 6; i++) {
        assert(catalog_next(&c, &rd) && rd.kind == order[i] && (i == 5 || rd.oid == 1));
    }
    assert(!catalog_next(&c, &rd));
}

static void rewant(void) {
    catalog c;
    loaded(&c);
    catalog_relation t = rel(&c, S, 1);
    page_total pt = page_total_open(&t);
    catalog_read rd;
    page_rewant(&c, S, 1, pt, false);
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_COLUMNS && rd.oid == 1);
    assert(catalog_next(&c, &rd) && catalog_next(&c, &rd) && rd.kind == CATALOG_READ_INDEXES);
    assert(!catalog_next(&c, &rd));
    page_rewant(&c, S, 1, pt, true);
    page_rewant(&c, S, 1, pt, true);
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_COUNT_TIMEOUT && rd.oid == 1);
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_COUNT);
    static const char *const n[] = {"2500"};
    result r = build(1, 1, n);
    assert(catalog_take(&c, rd, &r));
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_TIMEOUT && !catalog_next(&c, &rd));
    page_rewant(&c, S, 1, pt, true);
    assert(!catalog_next(&c, &rd));
    pt.count_state = PAGE_COUNT_KNOWN;
    page_rewant(&c, S, 2, pt, true);
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_COLUMNS && rd.oid == 2);
    assert(catalog_next(&c, &rd) && catalog_next(&c, &rd) && !catalog_next(&c, &rd));
    page_rewant(&c, S, 99, pt, true);
    assert(!catalog_next(&c, &rd));
}

static void gone(void) {
    catalog c;
    loaded(&c);
    assert(!page_gone(&c, S, 1) && page_gone(&c, S, 99) && page_gone(&c, 555, 1));
    assert(catalog_restart(&c));
    assert(!page_gone(&c, S, 99) && !page_gone(&c, 555, 1));
    static const char *const schemas[] = {"101", "My Schema", "100", "s"};
    catalog_read rd;
    result r = build(0, 0, NULL);
    assert(catalog_next(&c, &rd) && catalog_take(&c, rd, &r));
    static const char *const session[] = {"150019", "15.19", "on", "app", "robert", "localhost"};
    r = build(6, 1, session);
    assert(catalog_next(&c, &rd) && catalog_take(&c, rd, &r));
    r = build(2, 2, schemas);
    assert(catalog_next(&c, &rd) && catalog_take(&c, rd, &r));
    assert(page_gone(&c, 555, 1));
    assert(catalog_next(&c, &rd) && rd.kind == CATALOG_READ_RELATIONS);
    assert(!page_gone(&c, S, 99) && !page_gone(&c, S, 1));
}

int main(void) {
    sc_arena_init(&book, book_mem, sizeof book_mem);
    sc_arena_init(&rec, rec_mem, sizeof rec_mem);
    sc_arena_init(&keep, keep_mem, sizeof keep_mem);
    sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
    sc_arena_init(&defs, defs_mem, sizeof defs_mem);
    sc_arena_init(&out_a, out_mem, sizeof out_mem);
    page_queries();
    page_quoting();
    counts();
    count_timeout_fails();
    open_order();
    rewant();
    gone();
    pager();
    structure();
    puts("explorer_page_test: ok");
    return 0;
}
