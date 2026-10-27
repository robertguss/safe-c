/* Tests the explorer's catalog model (examples/explorer/catalog.c, plan U18):
 * identifier quoting, the read queue, results turned into records, a result
 * of the wrong shape refused, a full catalog arena told apart from it, a
 * restart that keeps expansions by name, and what the sidebar shows; a
 * function's definition read one row per line and joined, and a collapse
 * that keeps the records (U19). Results are built by hand, as the worker writes
 * them. Core code, built in. */
#include "../examples/explorer/proto.c"
#include "../examples/explorer/result.c"
#include "../examples/explorer/catalog.c"
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
static alignas(8) uint8_t defs_mem[1u << 16];
static sc_arena pool[2], book, rec, keep, tmp, defs;
static size_t cut_cell = SIZE_MAX; /* the cell `build` marks truncated */

static sc_str lit(sc_arena *a, const char *s) {
    size_t n = strlen(s);
    return n == 0 ? (sc_str){NULL, 0, a->epoch} : sc_str_from_lit(a, (const uint8_t *)s, n);
}

static bool same(const sc_arena *a, sc_str s, const char *want) {
    return sc_str_eq_lit(a, s, (const uint8_t *)want, strlen(want));
}

/* The result of `cols` columns and `rows` rows of `cells` (row-major; NULL is
 * an SQL NULL), as COLUMNS and one CHUNK. */
static result build(uint64_t cols, uint64_t rows, const char *const *cells) {
    result r;
    assert(sc_arena_reset(&book) && result_init(&r, &book, 4));
    sc_arena_init(&pool[0], pool_mem[0], ARENA_BYTES);
    proto_writer w;
    proto_header h = {PROTO_COLUMNS, 1, 1, 0, 0, 0};
    assert(proto_begin(&w, &pool[0], h) && proto_put_u64(&w, cols));
    for (uint64_t c = 0; c < cols; c++) {
        assert(sc_arena_reset(&tmp) && proto_put_str(&w, &tmp, lit(&tmp, "c")) &&
               proto_put_u64(&w, 25u));
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
        if (s == NULL) {
            assert(proto_put_u64(&w, PROTO_CELL_NULL) && proto_put_str(&w, &tmp, lit(&tmp, "")));
        } else {
            uint64_t meta = k == cut_cell ? (strlen(s) + 100u) | PROTO_CELL_TRUNCATED : strlen(s);
            assert(proto_put_u64(&w, meta) && proto_put_str(&w, &tmp, lit(&tmp, s)));
        }
    }
    assert(proto_end(&w) && result_add_chunk(&r, &pool[1], 1) == RESULT_ADDED);
    return r;
}

/* A finished result with no columns, as a SET returns. */
static result tag_only(void) {
    result r;
    assert(sc_arena_reset(&book) && result_init(&r, &book, 4));
    return r;
}

static catalog_read next_is(catalog *c, catalog_read_kind kind, uint32_t oid) {
    catalog_read r;
    assert(catalog_next(c, &r) && r.kind == kind && r.oid == oid);
    return r;
}

/* A restart, through its timeout read: the session and the schema list are
 * next. */
static void restarted(catalog *c) {
    assert(catalog_restart(c));
    result r = tag_only();
    assert(catalog_take(c, next_is(c, CATALOG_READ_TIMEOUT, 0), &r));
}

static const char *const session_row[] = {"150019", "15.19 (Debian 15.19-0+deb12u1)", "on",
                                          "app", "robert", "10.0.0.5"};
static const char *const schemas_rows[] = {"16384", "My Schema", "2200", "public"};

/* A fresh catalog through its session and schema list: "My Schema" (16384)
 * and public (2200). */
static void started(catalog *c) {
    assert(sc_arena_reset(&rec) && sc_arena_reset(&keep) && catalog_init(c, &rec, &keep, &defs));
    catalog_read none;
    assert(!catalog_next(c, &none));
    assert(catalog_restart(c));
    result r = tag_only();
    assert(catalog_take(c, next_is(c, CATALOG_READ_TIMEOUT, 0), &r));
    r = build(6, 1, session_row);
    assert(catalog_take(c, next_is(c, CATALOG_READ_SESSION, 0), &r));
    r = build(2, 2, schemas_rows);
    assert(catalog_take(c, next_is(c, CATALOG_READ_SCHEMAS, 0), &r));
    assert(c->schemas_load == CATALOG_LOAD_DONE && catalog_schema_count(c) == 2);
}

static void quote_ident(void) {
    sc_str out;
    assert(sc_arena_reset(&tmp));
    sc_str q;
    assert(catalog_quote_ident(&tmp, lit(&tmp, "users"), &tmp, &out) && same(&tmp, out, "\"users\""));
    assert(catalog_quote_ident(&tmp, lit(&tmp, "a\"b"), &tmp, &out) && same(&tmp, out, "\"a\"\"b\""));
    assert(catalog_quote_ident(&tmp, lit(&tmp, "My Schema"), &tmp, &out) &&
           same(&tmp, out, "\"My Schema\""));
    assert(catalog_quote_ident(&tmp, lit(&tmp, "\"\""), &tmp, &out) &&
           same(&tmp, out, "\"\"\"\"\"\""));
    q = out;
    const uint8_t nul[] = {'a', 0, 'b'};
    assert(!catalog_quote_ident(&tmp, sc_str_from_lit(&tmp, nul, 3), &tmp, &out));
    assert(out.ptr == q.ptr && out.len == q.len); /* left alone */
}

static void session_and_schemas(void) {
    catalog c;
    started(&c);
    assert(c.session.loaded && c.session.version_num == 150019 && c.session.standard_strings);
    assert(same(&rec, c.session.database, "app") && same(&rec, c.session.user, "robert"));
    assert(same(&rec, c.session.host, "10.0.0.5"));
    catalog_schema s;
    assert(catalog_schema_at(&c, 0, &s) && s.oid == 16384 && same(&rec, s.name, "My Schema"));
    assert(!s.expanded && s.load == CATALOG_LOAD_NONE);
    assert(catalog_schema_at(&c, 1, &s) && s.oid == 2200 && same(&rec, s.name, "public"));
    assert(!catalog_schema_at(&c, 2, &s));
    assert(!catalog_collapse(&c, 2));
    catalog_read none;
    assert(!catalog_next(&c, &none));
    /* standard_conforming_strings off reads as off. */
    const char *const off[] = {"90600", "9.6", "off", "app", "robert", "localhost"};
    restarted(&c);
    result r = build(6, 1, off);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_SESSION, 0), &r));
    assert(c.session.version_num == 90600 && !c.session.standard_strings);
}

/* relkinds r, p, v, m and f map to five kinds; reltuples -1 is unknown. */
static void relation_kinds(void) {
    catalog c;
    started(&c);
    assert(catalog_expand(&c, 1));
    catalog_read rr = next_is(&c, CATALOG_READ_RELATIONS, 2200);
    catalog_read fr = next_is(&c, CATALOG_READ_FUNCTIONS, 2200);
    catalog_schema s;
    assert(catalog_schema_at(&c, 1, &s) && s.expanded && s.load == CATALOG_LOAD_PENDING);
    const char *const rels[] = {"16400", "accounts", "r", "-1",
                                "16410", "events", "p", "0",
                                "16420", "active", "v", "-1",
                                "16430", "totals", "m", "2400",
                                "16440", "remote", "f", "17"};
    result r = build(4, 5, rels);
    assert(catalog_take(&c, rr, &r));
    assert(catalog_schema_at(&c, 1, &s) && s.load == CATALOG_LOAD_PENDING); /* functions out */
    const catalog_rel_kind kinds[] = {CATALOG_REL_TABLE, CATALOG_REL_PARTITIONED, CATALOG_REL_VIEW,
                                      CATALOG_REL_MATVIEW, CATALOG_REL_FOREIGN};
    for (size_t i = 0; i < 5; i++) {
        catalog_relation rel;
        assert(catalog_relation_at(&c, &s, i, &rel) && rel.kind == kinds[i]);
        assert(rel.detail == CATALOG_LOAD_NONE);
    }
    catalog_relation rel;
    assert(catalog_relation_at(&c, &s, 0, &rel) && rel.oid == 16400 && !rel.estimate_known);
    assert(same(&rec, rel.name, "accounts"));
    assert(catalog_relation_at(&c, &s, 1, &rel) && rel.estimate_known && rel.estimate == 0);
    assert(catalog_relation_at(&c, &s, 3, &rel) && rel.estimate_known && rel.estimate == 2400);
    assert(!catalog_relation_at(&c, &s, 5, &rel));
    const char *const fns[] = {"16500", "add", "f", "a integer, b integer", "integer",
                               "16510", "tidy", "p", "", NULL};
    r = build(5, 2, fns);
    assert(catalog_take(&c, fr, &r));
    assert(catalog_schema_at(&c, 1, &s) && s.load == CATALOG_LOAD_DONE);
    catalog_function f;
    assert(catalog_function_at(&c, &s, 0, &f) && f.oid == 16500 && f.kind == 'f');
    assert(same(&rec, f.args, "a integer, b integer") && same(&rec, f.result, "integer"));
    assert(catalog_function_at(&c, &s, 1, &f) && f.kind == 'p' && f.result.len == 0);
    size_t si = 9, ri = 9;
    assert(catalog_find_relation(&c, 2200, 16430, &si, &ri) && si == 1 && ri == 3);
    assert(!catalog_find_relation(&c, 2200, 16431, &si, &ri));
    assert(!catalog_find_relation(&c, 16384, 16430, &si, &ri));
    /* Loaded: expanding again queues nothing. */
    assert(catalog_expand(&c, 1));
    catalog_read none;
    assert(!catalog_next(&c, &none));
}

/* attidentity and attgenerated empty mean none, not NULL. */
static void columns_and_detail(void) {
    catalog c;
    started(&c);
    assert(catalog_expand(&c, 1));
    catalog_read rr = next_is(&c, CATALOG_READ_RELATIONS, 2200);
    catalog_read fr = next_is(&c, CATALOG_READ_FUNCTIONS, 2200);
    const char *const rels[] = {"16400", "accounts", "r", "3"};
    result r = build(4, 1, rels);
    assert(catalog_take(&c, rr, &r));
    r = build(5, 0, NULL);
    assert(catalog_take(&c, fr, &r));
    assert(!catalog_want_detail(&c, 2200, 99));
    assert(catalog_want_detail(&c, 2200, 16400));
    catalog_read cr = next_is(&c, CATALOG_READ_COLUMNS, 16400);
    catalog_read kr = next_is(&c, CATALOG_READ_CONSTRAINTS, 16400);
    catalog_read ir = next_is(&c, CATALOG_READ_INDEXES, 16400);
    assert(cr.schema == 2200);
    assert(catalog_want_detail(&c, 2200, 16400)); /* pending: nothing more */
    catalog_read none;
    assert(!catalog_next(&c, &none));
    const char *const cols[] = {
        "1", "id", "bigint", "t", NULL, "a", "", "1",
        "2", "code", "integer", "t", NULL, "d", "", "0",
        "3", "name", "text", "f", NULL, "", "", "0",
        "4", "total", "numeric(10,2)", "f", "(price * qty)", "", "s", "0",
        "5", "since", "date", "f", "CURRENT_DATE", "", "", "0",
        "6", "net", "numeric", "f", "(total * 0.8)", "", "v", "0"};
    r = build(8, 6, cols);
    assert(catalog_take(&c, cr, &r));
    catalog_schema s;
    catalog_relation rel;
    assert(catalog_schema_at(&c, 1, &s) && catalog_relation_at(&c, &s, 0, &rel));
    assert(rel.detail == CATALOG_LOAD_PENDING);
    catalog_column col;
    assert(catalog_column_at(&c, &rel, 0, &col) && col.identity == CATALOG_IDENTITY_ALWAYS);
    assert(col.not_null && !col.has_default && col.key_pos == 1 && same(&rec, col.type, "bigint"));
    assert(catalog_column_at(&c, &rel, 1, &col) && col.identity == CATALOG_IDENTITY_BY_DEFAULT);
    assert(catalog_column_at(&c, &rel, 2, &col) && col.identity == CATALOG_IDENTITY_NONE);
    assert(!col.generated && !col.not_null && col.key_pos == 0 && same(&rec, col.name, "name"));
    assert(catalog_column_at(&c, &rel, 3, &col) && col.generated && !col.generated_virtual);
    assert(col.has_default && same(&rec, col.default_expr, "(price * qty)"));
    assert(col.identity == CATALOG_IDENTITY_NONE);
    assert(catalog_column_at(&c, &rel, 4, &col) && !col.generated && col.num == 5);
    /* PG 18's virtual generated column. */
    assert(catalog_column_at(&c, &rel, 5, &col) && col.generated && col.generated_virtual);
    assert(same(&rec, col.default_expr, "(total * 0.8)"));
    assert(!catalog_column_at(&c, &rel, 6, &col));
    /* The definition cut short by the worker ends in an ellipsis. */
    const char *const cons[] = {
        "accounts_pkey", "p", "PRIMARY KEY (id, code)", "1 2", NULL,
        "accounts_owner_fkey", "f", "FOREIGN KEY (code) REFERENCES own", "2", "\"My Schema\".owners",
        "accounts_total_check", "c", "CHECK (total > 0::numeric)", "4", NULL,
        "accounts_x", "t", "TRIGGER", "", NULL};
    cut_cell = 7u;
    r = build(5, 4, cons);
    cut_cell = SIZE_MAX;
    assert(catalog_take(&c, kr, &r));
    const char *const idx[] = {"accounts_pkey", "t", "t",
                               "CREATE UNIQUE INDEX accounts_pkey ON public.accounts USING btree (id)"};
    r = build(4, 1, idx);
    assert(catalog_take(&c, ir, &r));
    assert(catalog_relation_at(&c, &s, 0, &rel) && rel.detail == CATALOG_LOAD_DONE);
    catalog_constraint k;
    uint32_t at = 0;
    assert(catalog_constraint_at(&c, &rel, 0, &k) && k.type == 'p' && k.target.len == 0);
    assert(sc_tslice_len(k.columns) == 2);
    assert(SC_TSLICE_GET(uint32_t, &rec, k.columns, 0, &at) && at == 1);
    assert(SC_TSLICE_GET(uint32_t, &rec, k.columns, 1, &at) && at == 2);
    assert(catalog_constraint_at(&c, &rel, 1, &k) && k.type == 'f');
    assert(same(&rec, k.def, "FOREIGN KEY (code) REFERENCES own\xE2\x80\xA6"));
    assert(same(&rec, k.target, "\"My Schema\".owners") && sc_tslice_len(k.columns) == 1);
    assert(SC_TSLICE_GET(uint32_t, &rec, k.columns, 0, &at) && at == 2);
    assert(catalog_constraint_at(&c, &rel, 2, &k) && k.type == 'c');
    assert(same(&rec, k.def, "CHECK (total > 0::numeric)"));
    assert(SC_TSLICE_GET(uint32_t, &rec, k.columns, 0, &at) && at == 4);
    assert(catalog_constraint_at(&c, &rel, 3, &k) && sc_tslice_len(k.columns) == 0);
    catalog_index ix;
    assert(catalog_index_at(&c, &rel, 0, &ix) && ix.primary && ix.unique);
    assert(same(&rec, ix.name, "accounts_pkey"));
}

/* Expands public (schema 1) and pops its two reads. */
static void expand_reads(catalog *c, catalog_read *rr, catalog_read *fr) {
    assert(catalog_expand(c, 1));
    *rr = next_is(c, CATALOG_READ_RELATIONS, 2200);
    *fr = next_is(c, CATALOG_READ_FUNCTIONS, 2200);
}

/* A relations result with one column fewer than the read selects stores
 * nothing: the schema shows an error, not shifted values. So does a cell
 * that does not parse. */
static void wrong_shape(void) {
    catalog c;
    started(&c);
    catalog_read rr, fr;
    expand_reads(&c, &rr, &fr);
    const char *const short_rels[] = {"16400", "accounts", "r"};
    result r = build(3, 1, short_rels);
    assert(!catalog_take(&c, rr, &r));
    catalog_schema s;
    assert(catalog_schema_at(&c, 1, &s) && s.load == CATALOG_LOAD_FAILED && s.error.len > 0);
    catalog_relation rel;
    assert(!catalog_relation_at(&c, &s, 0, &rel));
    r = build(5, 0, NULL);
    assert(catalog_take(&c, fr, &r));
    assert(catalog_schema_at(&c, 1, &s) && s.load == CATALOG_LOAD_FAILED); /* still */
    /* Expanding again asks again. A bad kind, a bad or too big oid and a NULL
     * name fail it too. */
    const char *const bad[4][4] = {{"16400", "accounts", "S", "1"},
                                   {"16x00", "accounts", "r", "1"},
                                   {"4294967296", "accounts", "r", "1"},
                                   {"16400", NULL, "r", "1"}};
    for (size_t i = 0; i < 4; i++) {
        expand_reads(&c, &rr, &fr);
        r = build(4, 1, bad[i]);
        assert(!catalog_take(&c, rr, &r));
        r = build(5, 0, NULL);
        assert(catalog_take(&c, fr, &r));
        assert(catalog_schema_at(&c, 1, &s) && s.load == CATALOG_LOAD_FAILED);
        assert(!catalog_relation_at(&c, &s, 0, &rel));
    }
    /* The schema list one column short fails the list. */
    restarted(&c);
    r = build(6, 1, session_row);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_SESSION, 0), &r));
    const char *const one_col[] = {"2200"};
    r = build(1, 1, one_col);
    assert(!catalog_take(&c, next_is(&c, CATALOG_READ_SCHEMAS, 0), &r));
    assert(c.schemas_load == CATALOG_LOAD_FAILED && catalog_schema_count(&c) == 0);
    /* An identity code other than empty, a and d. */
    started(&c);
    expand_reads(&c, &rr, &fr);
    const char *const rels[] = {"16400", "accounts", "r", "3"};
    r = build(4, 1, rels);
    assert(catalog_take(&c, rr, &r));
    assert(catalog_want_detail(&c, 2200, 16400));
    catalog_read cr = next_is(&c, CATALOG_READ_COLUMNS, 16400);
    const char *const cols[] = {"1", "id", "bigint", "t", NULL, "x", "", "0"};
    r = build(8, 1, cols);
    assert(!catalog_take(&c, cr, &r));
    assert(catalog_schema_at(&c, 1, &s) && catalog_relation_at(&c, &s, 0, &rel));
    assert(rel.detail == CATALOG_LOAD_FAILED && rel.error.len > 0);
    assert(same(&rec, rel.error, "The catalog read returned rows of an unexpected shape."));
}

/* Detail rows that must not parse: each fails the relation's detail. */
static void detail_refused(const char *const *cells, catalog_read_kind kind, uint64_t cols) {
    catalog c;
    catalog_read rr, fr;
    started(&c);
    expand_reads(&c, &rr, &fr);
    const char *const rels[] = {"16400", "accounts", "r", "3"};
    result r = build(4, 1, rels);
    assert(catalog_take(&c, rr, &r));
    assert(catalog_want_detail(&c, 2200, 16400));
    catalog_read cr = next_is(&c, CATALOG_READ_COLUMNS, 16400);
    catalog_read kr = next_is(&c, CATALOG_READ_CONSTRAINTS, 16400);
    catalog_read ir = next_is(&c, CATALOG_READ_INDEXES, 16400);
    r = build(cols, 1, cells);
    catalog_read which = kind == CATALOG_READ_COLUMNS ? cr : kind == CATALOG_READ_CONSTRAINTS ? kr : ir;
    assert(!catalog_take(&c, which, &r));
    catalog_schema s;
    catalog_relation rel;
    assert(catalog_schema_at(&c, 1, &s) && catalog_relation_at(&c, &s, 0, &rel));
    assert(rel.detail == CATALOG_LOAD_FAILED);
}

/* The parsers are strict: a bool other than t or f, an attgenerated code
 * other than empty, s and v, a column list that is not attnums, a truncated
 * number, and a session of two rows are refused. */
static void strict_parses(void) {
    const char *const bool_x[] = {"accounts_pkey", "x", "t", "CREATE UNIQUE INDEX"};
    detail_refused(bool_x, CATALOG_READ_INDEXES, 4);
    const char *const bool_long[] = {"accounts_pkey", "true", "t", "CREATE UNIQUE INDEX"};
    detail_refused(bool_long, CATALOG_READ_INDEXES, 4);
    const char *const gen_q[] = {"1", "id", "bigint", "t", NULL, "", "q", "0"};
    detail_refused(gen_q, CATALOG_READ_COLUMNS, 8);
    const char *const bad_lists[][5] = {{"k", "u", "UNIQUE (a)", "1 x", NULL},
                                        {"k", "u", "UNIQUE (a)", "1  2", NULL},
                                        {"k", "u", "UNIQUE (a)", "1 ", NULL},
                                        {"k", "u", "UNIQUE (a)", "-1", NULL},
                                        {"k", "u", "UNIQUE (a)", "4294967296", NULL},
                                        {"k", "u", "UNIQUE (a)", NULL, NULL}};
    for (size_t i = 0; i < 6; i++) {
        detail_refused(bad_lists[i], CATALOG_READ_CONSTRAINTS, 5);
    }
    /* A number cut short is not that number. */
    const char *const cols[] = {"1", "id", "bigint", "t", NULL, "", "", "12"};
    cut_cell = 7u;
    detail_refused(cols, CATALOG_READ_COLUMNS, 8);
    cut_cell = SIZE_MAX;

    catalog c;
    assert(sc_arena_reset(&rec) && sc_arena_reset(&keep) && catalog_init(&c, &rec, &keep, &defs));
    restarted(&c);
    const char *const two[] = {"150019", "15.19", "on", "app", "robert", "localhost",
                               "150019", "15.19", "on", "app", "robert", "localhost"};
    result r = build(6, 2, two);
    assert(!catalog_take(&c, next_is(&c, CATALOG_READ_SESSION, 0), &r));
    assert(c.schemas_load == CATALOG_LOAD_FAILED && !c.session.loaded);
}

/* A read kind the catalog does not list is refused, never parsed as
 * another kind's rows. */
static void unknown_kind(void) {
    catalog c;
    catalog_read rr, fr;
    started(&c);
    expand_reads(&c, &rr, &fr);
    const char *const rels[] = {"16400", "accounts", "r", "3"};
    result r = build(4, 1, rels);
    assert(catalog_take(&c, rr, &r));
    assert(catalog_want_detail(&c, 2200, 16400));
    catalog_read ir = next_is(&c, CATALOG_READ_COLUMNS, 16400);
    ir.kind = (catalog_read_kind)99;
    const char *const idx[] = {"accounts_pkey", "t", "t", "CREATE UNIQUE INDEX"};
    r = build(4, 1, idx);
    assert(!catalog_take(&c, ir, &r));
    sc_str sql;
    assert(sc_arena_reset(&tmp) && !catalog_sql(ir, &tmp, &sql));
}

static void definition(void) {
    catalog c;
    started(&c);
    uint32_t oid = 0;
    bool ok = false;
    sc_str text;
    assert(!catalog_take_definition(&c, &oid, &ok, &text));
    assert(catalog_want_definition(&c, 16500));
    catalog_read d = next_is(&c, CATALOG_READ_DEFINITION, 16500);
    sc_str sql;
    size_t at = 0;
    assert(sc_arena_reset(&tmp) && catalog_sql(d, &tmp, &sql));
    assert(sc_str_find(&tmp, sql, lit(&tmp, "pg_get_functiondef(16500::pg_catalog.oid)"), &at));
    assert(!catalog_take_definition(&c, &oid, &ok, &text));
    const char *const lines[] = {"CREATE OR REPLACE FUNCTION public.add(a integer, b integer)",
                                 " RETURNS integer", "", " LANGUAGE sql", "AS $function$SELECT a + b$function$"};
    result r = build(1, 5, lines);
    assert(catalog_take(&c, d, &r));
    assert(catalog_take_definition(&c, &oid, &ok, &text) && oid == 16500 && ok);
    assert(same(&defs, text, "CREATE OR REPLACE FUNCTION public.add(a integer, b integer)\n"
                            " RETURNS integer\n\n LANGUAGE sql\nAS $function$SELECT a + b$function$"));
    assert(!catalog_take_definition(&c, &oid, &ok, &text));
    assert(catalog_want_definition(&c, 1) && catalog_want_definition(&c, 2));
    catalog_read old = next_is(&c, CATALOG_READ_DEFINITION, 1);
    catalog_read cur = next_is(&c, CATALOG_READ_DEFINITION, 2);
    r = build(1, 1, lines);
    assert(!catalog_take(&c, old, &r));
    assert(!catalog_take_definition(&c, &oid, &ok, &text));
    r = build(1, 1, lines);
    assert(catalog_take(&c, cur, &r));
    assert(catalog_take_definition(&c, &oid, &ok, &text) && oid == 2 && ok);
    assert(same(&defs, text, "CREATE OR REPLACE FUNCTION public.add(a integer, b integer)"));
    assert(catalog_want_definition(&c, 3));
    d = next_is(&c, CATALOG_READ_DEFINITION, 3);
    cut_cell = 1u;
    r = build(1, 2, lines);
    cut_cell = SIZE_MAX;
    assert(!catalog_take(&c, d, &r));
    assert(catalog_take_definition(&c, &oid, &ok, &text) && oid == 3 && !ok);
    assert(same(&defs, text, "A line of the definition is longer than the sidebar reads."));
    const char *const two[] = {"a", "b"};
    const char *const null_line[] = {NULL};
    for (int k = 0; k < 4; k++) {
        assert(catalog_want_definition(&c, 4));
        d = next_is(&c, CATALOG_READ_DEFINITION, 4);
        if (k == 3) {
            assert(sc_arena_reset(&tmp));
            catalog_fail(&c, d, &tmp, lit(&tmp, "\"sum\" is an aggregate function"));
        } else {
            r = k == 0 ? build(2, 1, two) : k == 1 ? build(1, 0, NULL) : build(1, 1, null_line);
            assert(!catalog_take(&c, d, &r));
        }
        assert(catalog_take_definition(&c, &oid, &ok, &text) && oid == 4 && !ok && text.len > 0);
        assert(k < 3 || same(&defs, text, "\"sum\" is an aggregate function"));
    }
    assert(c.schemas_load == CATALOG_LOAD_DONE && c.error.len == 0);
    assert(catalog_want_definition(&c, 5));
    d = next_is(&c, CATALOG_READ_DEFINITION, 5);
    restarted(&c);
    r = build(1, 1, lines);
    assert(!catalog_take(&c, d, &r));
    assert(!catalog_take_definition(&c, &oid, &ok, &text));
}

static void definitions_stay_flat(void) {
    catalog c;
    started(&c);
    const char *const lines[] = {"CREATE OR REPLACE FUNCTION public.add(a integer, b integer)",
                                 " RETURNS integer", " LANGUAGE sql", "AS $function$SELECT a + b$function$"};
    uint32_t oid = 0;
    bool ok = false;
    sc_str text;
    size_t used = 0;
    for (uint32_t k = 0; k < 64; k++) {
        assert(catalog_want_definition(&c, 16500));
        catalog_read d = next_is(&c, CATALOG_READ_DEFINITION, 16500);
        result r = build(1, 4, lines);
        assert(catalog_take(&c, d, &r));
        assert(catalog_take_definition(&c, &oid, &ok, &text) && ok && text.len > 100);
        used = k == 0 ? rec.used : used;
        assert(rec.used == used);
    }
    static alignas(8) uint8_t small_mem[128];
    sc_arena small;
    sc_arena_init(&small, small_mem, sizeof small_mem);
    assert(sc_arena_reset(&rec) && sc_arena_reset(&keep) && catalog_init(&c, &rec, &keep, &small));
    restarted(&c);
    catalog_read skip;
    assert(catalog_next(&c, &skip) && catalog_next(&c, &skip));
    char x[61], y[68];
    memset(x, 'x', 60);
    x[60] = 0;
    memset(y, 'y', 67);
    y[67] = 0;
    const char *const fits[] = {x, y};
    assert(catalog_want_definition(&c, 7));
    result r = build(1, 2, fits);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_DEFINITION, 7), &r));
    assert(catalog_take_definition(&c, &oid, &ok, &text) && ok && text.len == 128);
    y[67] = 'y';
    char z[69];
    memcpy(z, y, 68);
    z[68] = 0;
    const char *const over[] = {x, z};
    assert(catalog_want_definition(&c, 8));
    r = build(1, 2, over);
    assert(!catalog_take(&c, next_is(&c, CATALOG_READ_DEFINITION, 8), &r));
    assert(catalog_take_definition(&c, &oid, &ok, &text) && oid == 8 && !ok);
    assert(same(&small, text, "The definition is longer than the editor holds."));
}

static void definition_text(void) {
    catalog c;
    started(&c);
    uint32_t oid = 0;
    const sc_arena *ta = NULL;
    sc_str text;
    assert(sc_arena_reset(&tmp) && !catalog_definition_text(&c, &tmp, &oid, &ta, &text));
    const char *const lines[] = {"CREATE FUNCTION f()", "AS $$x$$"};
    assert(catalog_want_definition(&c, 9));
    result r = build(1, 2, lines);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_DEFINITION, 9), &r));
    assert(catalog_definition_text(&c, &tmp, &oid, &ta, &text) && oid == 9 && ta == &defs);
    assert(same(&defs, text, "CREATE FUNCTION f()\nAS $$x$$"));
    assert(!catalog_definition_text(&c, &tmp, &oid, &ta, &text));
    assert(catalog_want_definition(&c, 10));
    catalog_fail(&c, next_is(&c, CATALOG_READ_DEFINITION, 10), &tmp,
                 lit(&tmp, "\"sum\" is an aggregate function\nDETAIL:\x01x\xff"));
    assert(catalog_definition_text(&c, &tmp, &oid, &ta, &text) && oid == 10 && ta == &tmp);
    assert(same(&tmp, text, "-- The function's definition could not be read: \"sum\" is an aggregate function "
                            "DETAIL: x "));
    static char long_reason[CATALOG_NOTE_MAX + 101];
    memset(long_reason, 'e', CATALOG_NOTE_MAX + 100);
    char want[CATALOG_NOTE_MAX + 128];
    int n = snprintf(want, sizeof want, "-- The function's definition could not be read: %.*s\xE2\x80\xA6",
                     (int)CATALOG_NOTE_MAX, long_reason);
    assert(n > 0 && (size_t)n < sizeof want);
    assert(catalog_want_definition(&c, 11));
    catalog_fail(&c, next_is(&c, CATALOG_READ_DEFINITION, 11), &tmp, lit(&tmp, long_reason));
    assert(catalog_definition_text(&c, &tmp, &oid, &ta, &text) && same(&tmp, text, want));
}

/* A catalog arena with no room left for a read's records says so, and to
 * reload, rather than blaming the rows. */
static void out_of_room(void) {
    static alignas(8) uint8_t small_mem[1u << 14];
    sc_arena small;
    sc_arena_init(&small, small_mem, sizeof small_mem);
    catalog c;
    assert(sc_arena_reset(&keep) && catalog_init(&c, &small, &keep, &defs));
    restarted(&c);
    result r = build(6, 1, session_row);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_SESSION, 0), &r));
    r = build(2, 2, schemas_rows);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_SCHEMAS, 0), &r));
    catalog_read rr, fr;
    expand_reads(&c, &rr, &fr);
    assert(small.cap - small.used > 64u);
    (void)sc_arena_alloc(&small, small.cap - small.used - 64u);
    const char *const rels[] = {"16400", "accounts", "r", "3", "16410", "events", "r", "3"};
    r = build(4, 2, rels);
    assert(!catalog_take(&c, rr, &r));
    catalog_schema s;
    assert(catalog_schema_at(&c, 1, &s) && s.load == CATALOG_LOAD_FAILED);
    assert(same(&small, s.error, "The sidebar has no room for more catalog records; reload."));
    /* A reload frees the arena: the same rows load. */
    restarted(&c);
    r = build(6, 1, session_row);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_SESSION, 0), &r));
    r = build(2, 2, schemas_rows);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_SCHEMAS, 0), &r));
    rr = next_is(&c, CATALOG_READ_RELATIONS, 2200);
    (void)next_is(&c, CATALOG_READ_FUNCTIONS, 2200);
    r = build(4, 2, rels);
    assert(catalog_take(&c, rr, &r));
}

/* The metadata link's first read sets its 10 s statement_timeout (KTD21):
 * a SET, so it returns no columns; rows instead fail the bootstrap, and so
 * does an error. */
static void timeout_read(void) {
    catalog c;
    assert(sc_arena_reset(&rec) && sc_arena_reset(&keep) && catalog_init(&c, &rec, &keep, &defs));
    assert(catalog_restart(&c));
    catalog_read t = next_is(&c, CATALOG_READ_TIMEOUT, 0);
    sc_str sql;
    assert(sc_arena_reset(&tmp) && catalog_sql(t, &tmp, &sql));
    assert(same(&tmp, sql, "SET statement_timeout = '10s'"));
    (void)next_is(&c, CATALOG_READ_SESSION, 0);
    (void)next_is(&c, CATALOG_READ_SCHEMAS, 0);
    result r = build(6, 1, session_row);
    assert(!catalog_take(&c, t, &r));
    assert(c.schemas_load == CATALOG_LOAD_FAILED);
    assert(catalog_restart(&c));
    t = next_is(&c, CATALOG_READ_TIMEOUT, 0);
    assert(sc_arena_reset(&tmp));
    catalog_fail(&c, t, &tmp, lit(&tmp, "permission denied"));
    assert(c.schemas_load == CATALOG_LOAD_FAILED && same(&rec, c.error, "permission denied"));
}

/* catalog_fail marks the read's target failed with the message. */
static void failures(void) {
    catalog c;
    assert(sc_arena_reset(&rec) && sc_arena_reset(&keep) && catalog_init(&c, &rec, &keep, &defs));
    restarted(&c);
    catalog_read sr = next_is(&c, CATALOG_READ_SESSION, 0);
    assert(sc_arena_reset(&tmp));
    catalog_fail(&c, sr, &tmp, lit(&tmp, "canceling statement due to statement timeout"));
    assert(c.schemas_load == CATALOG_LOAD_FAILED);
    assert(same(&rec, c.error, "canceling statement due to statement timeout"));
    assert(catalog_shown(&c, CATALOG_LINK_UP, CATALOG_LINK_UP) == CATALOG_SHOW_ERROR);
    /* The schema list still comes in, but the failure stands until a restart. */
    result r = build(2, 2, schemas_rows);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_SCHEMAS, 0), &r));
    assert(c.schemas_load == CATALOG_LOAD_FAILED);

    started(&c);
    assert(catalog_expand(&c, 0));
    catalog_read rr = next_is(&c, CATALOG_READ_RELATIONS, 16384);
    assert(sc_arena_reset(&tmp));
    catalog_fail(&c, rr, &tmp, lit(&tmp, "lock timeout"));
    catalog_schema s;
    assert(catalog_schema_at(&c, 0, &s) && s.load == CATALOG_LOAD_FAILED);
    assert(same(&rec, s.error, "lock timeout"));
    assert(catalog_shown(&c, CATALOG_LINK_UP, CATALOG_LINK_UP) == CATALOG_SHOW_READY);

    /* A read from before a restart that then fails changes nothing. */
    assert(catalog_restart(&c));
    catalog_read stale = next_is(&c, CATALOG_READ_TIMEOUT, 0);
    assert(catalog_restart(&c));
    assert(sc_arena_reset(&tmp));
    catalog_fail(&c, stale, &tmp, lit(&tmp, "canceling statement due to user request"));
    assert(c.schemas_load == CATALOG_LOAD_PENDING && c.error.len == 0);
}

/* A restart forgets the records and keeps expansions by name: "My Schema"
 * comes back under a new oid and is expanded again; a read from before the
 * restart is dropped. */
static void restart_keeps_expansion(void) {
    catalog c;
    started(&c);
    assert(catalog_expand(&c, 0));
    catalog_read old = next_is(&c, CATALOG_READ_RELATIONS, 16384);
    restarted(&c);
    catalog_schema s;
    assert(catalog_schema_count(&c) == 0 && c.schemas_load == CATALOG_LOAD_PENDING);
    const char *const rels[] = {"16400", "accounts", "r", "3"};
    result r = build(4, 1, rels);
    assert(!catalog_take(&c, old, &r)); /* dropped */
    r = build(6, 1, session_row);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_SESSION, 0), &r));
    const char *const again[] = {"2200", "public", "17000", "My Schema", "17010", "zeta"};
    r = build(2, 3, again);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_SCHEMAS, 0), &r));
    assert(catalog_schema_at(&c, 0, &s) && !s.expanded);
    assert(catalog_schema_at(&c, 1, &s) && s.expanded && s.oid == 17000 &&
           s.load == CATALOG_LOAD_PENDING);
    assert(catalog_schema_at(&c, 2, &s) && !s.expanded);
    (void)next_is(&c, CATALOG_READ_RELATIONS, 17000);
    (void)next_is(&c, CATALOG_READ_FUNCTIONS, 17000);
    catalog_read none;
    assert(!catalog_next(&c, &none));
}

/* A second restart before the first one's schema list lands (a failed
 * session read, then Retry) still keeps the expansion. */
static void restart_twice_keeps_expansion(void) {
    catalog c;
    started(&c);
    assert(catalog_expand(&c, 0));
    restarted(&c);
    assert(sc_arena_reset(&tmp));
    catalog_fail(&c, next_is(&c, CATALOG_READ_SESSION, 0), &tmp, lit(&tmp, "timeout"));
    assert(c.schemas_load == CATALOG_LOAD_FAILED);
    restarted(&c);
    result r = build(6, 1, session_row);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_SESSION, 0), &r));
    r = build(2, 2, schemas_rows);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_SCHEMAS, 0), &r));
    catalog_schema s;
    assert(catalog_schema_at(&c, 0, &s) && s.expanded && s.load == CATALOG_LOAD_PENDING);
    assert(catalog_schema_at(&c, 1, &s) && !s.expanded);
    (void)next_is(&c, CATALOG_READ_RELATIONS, 16384);
}

/* The SQL filters by oid literals, and schema names never reach it. */
static void sql_text(void) {
    sc_str sql;
    catalog_read r = {CATALOG_READ_RELATIONS, 4294967295u, 0, 0};
    assert(sc_arena_reset(&tmp) && catalog_sql(r, &tmp, &sql));
    size_t at = 0;
    assert(sc_str_find(&tmp, sql, lit(&tmp, "4294967295"), &at));
    assert(sc_str_find(&tmp, sql, lit(&tmp, "relispartition"), &at));
    catalog_read fns = {CATALOG_READ_FUNCTIONS, 7, 0, 0};
    assert(sc_arena_reset(&tmp) && catalog_sql(fns, &tmp, &sql));
    assert(sc_str_find(&tmp, sql, lit(&tmp, "p.prokind <> 'a'"), &at));
    catalog_read kinds[] = {{CATALOG_READ_TIMEOUT, 0, 0, 0},
                            {CATALOG_READ_SESSION, 0, 0, 0},   {CATALOG_READ_SCHEMAS, 0, 0, 0},
                            {CATALOG_READ_FUNCTIONS, 7, 0, 0}, {CATALOG_READ_COLUMNS, 7, 1, 0},
                            {CATALOG_READ_CONSTRAINTS, 7, 1, 0}, {CATALOG_READ_INDEXES, 7, 1, 0},
                            {CATALOG_READ_DEFINITION, 7, 0, 0}};
    for (size_t i = 0; i < 8; i++) {
        assert(sc_arena_reset(&tmp) && catalog_sql(kinds[i], &tmp, &sql) && sql.len > 20);
    }
}

/* Plan U18 step 1: the query link down or failed shows the tree greyed and
 * Disconnected, whatever the metadata link does; with it up, a failed
 * metadata link is an error with its own Retry. */
static void sidebar_states(void) {
    catalog c;
    assert(sc_arena_reset(&rec) && sc_arena_reset(&keep) && catalog_init(&c, &rec, &keep, &defs));
    const catalog_link all[] = {CATALOG_LINK_DOWN, CATALOG_LINK_CONNECTING, CATALOG_LINK_UP,
                                CATALOG_LINK_FAILED};
    for (size_t m = 0; m < 4; m++) {
        assert(catalog_shown(&c, CATALOG_LINK_DOWN, all[m]) == CATALOG_SHOW_DISCONNECTED);
        assert(catalog_shown(&c, CATALOG_LINK_FAILED, all[m]) == CATALOG_SHOW_DISCONNECTED);
        assert(catalog_shown(&c, CATALOG_LINK_CONNECTING, all[m]) == CATALOG_SHOW_CONNECTING);
    }
    assert(catalog_shown(&c, CATALOG_LINK_UP, CATALOG_LINK_FAILED) == CATALOG_SHOW_ERROR);
    assert(catalog_shown(&c, CATALOG_LINK_UP, CATALOG_LINK_DOWN) == CATALOG_SHOW_LOADING);
    assert(catalog_shown(&c, CATALOG_LINK_UP, CATALOG_LINK_CONNECTING) == CATALOG_SHOW_LOADING);
    assert(catalog_shown(&c, CATALOG_LINK_UP, CATALOG_LINK_UP) == CATALOG_SHOW_LOADING);
    started(&c);
    assert(catalog_shown(&c, CATALOG_LINK_UP, CATALOG_LINK_UP) == CATALOG_SHOW_READY);
    assert(catalog_shown(&c, CATALOG_LINK_UP, CATALOG_LINK_FAILED) == CATALOG_SHOW_ERROR);
    assert(catalog_shown(&c, CATALOG_LINK_DOWN, CATALOG_LINK_UP) == CATALOG_SHOW_DISCONNECTED);
}

/* A queue that fills fails the schema it could not queue for. */
static void full_queue(void) {
    catalog c;
    assert(sc_arena_reset(&rec) && sc_arena_reset(&keep) && catalog_init(&c, &rec, &keep, &defs));
    restarted(&c);
    result r = build(6, 1, session_row);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_SESSION, 0), &r));
    static const char *rows[2u * (CATALOG_QUEUE / 2u + 1u)];
    static char names[CATALOG_QUEUE / 2u + 1u][16];
    size_t n = CATALOG_QUEUE / 2u + 1u;
    static char oids[CATALOG_QUEUE / 2u + 1u][16];
    for (size_t i = 0; i < n; i++) {
        snprintf(oids[i], sizeof oids[i], "%zu", 20000u + i);
        snprintf(names[i], sizeof names[i], "s%03zu", i);
        rows[2 * i] = oids[i];
        rows[2 * i + 1] = names[i];
    }
    r = build(2, n, rows);
    assert(catalog_take(&c, next_is(&c, CATALOG_READ_SCHEMAS, 0), &r));
    for (size_t i = 0; i + 1u < n; i++) {
        assert(catalog_expand(&c, i));
    }
    assert(!catalog_expand(&c, n - 1u));
    catalog_schema s;
    assert(catalog_schema_at(&c, n - 1u, &s) && s.load == CATALOG_LOAD_FAILED && s.expanded);
}

int main(void) {
    sc_arena_init(&book, book_mem, sizeof book_mem);
    sc_arena_init(&rec, rec_mem, sizeof rec_mem);
    sc_arena_init(&keep, keep_mem, sizeof keep_mem);
    sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
    sc_arena_init(&defs, defs_mem, sizeof defs_mem);
    quote_ident();
    session_and_schemas();
    relation_kinds();
    columns_and_detail();
    wrong_shape();
    failures();
    restart_keeps_expansion();
    restart_twice_keeps_expansion();
    strict_parses();
    unknown_kind();
    out_of_room();
    timeout_read();
    sql_text();
    sidebar_states();
    full_queue();
    definition();
    definitions_stay_flat();
    definition_text();
    puts("explorer_catalog_test: ok");
    return 0;
}
