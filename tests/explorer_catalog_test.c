/* Tests the explorer's catalog model (examples/explorer/catalog.c, plan U18):
 * identifier quoting, the read queue, results turned into records, a result
 * of the wrong shape refused, a restart that keeps expansions by name, and
 * what the sidebar shows. Results are built by hand, as the worker writes
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
static sc_arena pool[2], book, rec, keep, tmp;

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
            assert(proto_put_u64(&w, strlen(s)) && proto_put_str(&w, &tmp, lit(&tmp, s)));
        }
    }
    assert(proto_end(&w) && result_add_chunk(&r, &pool[1], 1) == RESULT_ADDED);
    return r;
}

static catalog_read next_is(catalog *c, catalog_read_kind kind, uint32_t oid) {
    catalog_read r;
    assert(catalog_next(c, &r) && r.kind == kind && r.oid == oid);
    return r;
}

static const char *const session_row[] = {"150019", "15.19 (Debian 15.19-0+deb12u1)", "on",
                                          "app", "robert"};
static const char *const schemas_rows[] = {"16384", "My Schema", "2200", "public"};

/* A fresh catalog through its session and schema list: "My Schema" (16384)
 * and public (2200). */
static void started(catalog *c) {
    assert(sc_arena_reset(&rec) && sc_arena_reset(&keep) && catalog_init(c, &rec, &keep));
    catalog_read none;
    assert(!catalog_next(c, &none));
    assert(catalog_restart(c));
    result r = build(5, 1, session_row);
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
    catalog_schema s;
    assert(catalog_schema_at(&c, 0, &s) && s.oid == 16384 && same(&rec, s.name, "My Schema"));
    assert(!s.expanded && s.load == CATALOG_LOAD_NONE);
    assert(catalog_schema_at(&c, 1, &s) && s.oid == 2200 && same(&rec, s.name, "public"));
    assert(!catalog_schema_at(&c, 2, &s));
    catalog_read none;
    assert(!catalog_next(&c, &none));
    /* standard_conforming_strings off reads as off. */
    const char *const off[] = {"90600", "9.6", "off", "app", "robert"};
    assert(catalog_restart(&c));
    result r = build(5, 1, off);
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
        "5", "since", "date", "f", "CURRENT_DATE", "", "", "0"};
    r = build(8, 5, cols);
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
    assert(catalog_column_at(&c, &rel, 3, &col) && col.generated && col.has_default);
    assert(same(&rec, col.default_expr, "(price * qty)") && col.identity == CATALOG_IDENTITY_NONE);
    assert(catalog_column_at(&c, &rel, 4, &col) && !col.generated && col.num == 5);
    assert(!catalog_column_at(&c, &rel, 5, &col));
    const char *const cons[] = {"accounts_pkey", "p", "PRIMARY KEY (id)",
                                "accounts_total_check", "c", "CHECK (total > 0::numeric)"};
    r = build(3, 2, cons);
    assert(catalog_take(&c, kr, &r));
    const char *const idx[] = {"accounts_pkey", "t", "t",
                               "CREATE UNIQUE INDEX accounts_pkey ON public.accounts USING btree (id)"};
    r = build(4, 1, idx);
    assert(catalog_take(&c, ir, &r));
    assert(catalog_relation_at(&c, &s, 0, &rel) && rel.detail == CATALOG_LOAD_DONE);
    catalog_constraint k;
    assert(catalog_constraint_at(&c, &rel, 1, &k) && k.type == 'c');
    assert(same(&rec, k.def, "CHECK (total > 0::numeric)"));
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
    assert(catalog_restart(&c));
    r = build(5, 1, session_row);
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
}

/* catalog_fail marks the read's target failed with the message. */
static void failures(void) {
    catalog c;
    assert(sc_arena_reset(&rec) && sc_arena_reset(&keep) && catalog_init(&c, &rec, &keep));
    assert(catalog_restart(&c));
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
}

/* A restart forgets the records and keeps expansions by name: "My Schema"
 * comes back under a new oid and is expanded again; a read from before the
 * restart is dropped. */
static void restart_keeps_expansion(void) {
    catalog c;
    started(&c);
    assert(catalog_expand(&c, 0));
    catalog_read old = next_is(&c, CATALOG_READ_RELATIONS, 16384);
    assert(catalog_restart(&c));
    catalog_schema s;
    assert(catalog_schema_count(&c) == 0 && c.schemas_load == CATALOG_LOAD_PENDING);
    const char *const rels[] = {"16400", "accounts", "r", "3"};
    result r = build(4, 1, rels);
    assert(!catalog_take(&c, old, &r)); /* dropped */
    r = build(5, 1, session_row);
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

/* The SQL filters by oid literals, and schema names never reach it. */
static void sql_text(void) {
    sc_str sql;
    catalog_read r = {CATALOG_READ_RELATIONS, 4294967295u, 0, 0};
    assert(sc_arena_reset(&tmp) && catalog_sql(r, &tmp, &sql));
    size_t at = 0;
    assert(sc_str_find(&tmp, sql, lit(&tmp, "4294967295"), &at));
    assert(sc_str_find(&tmp, sql, lit(&tmp, "relispartition"), &at));
    catalog_read kinds[] = {{CATALOG_READ_SESSION, 0, 0, 0},   {CATALOG_READ_SCHEMAS, 0, 0, 0},
                            {CATALOG_READ_FUNCTIONS, 7, 0, 0}, {CATALOG_READ_COLUMNS, 7, 1, 0},
                            {CATALOG_READ_CONSTRAINTS, 7, 1, 0}, {CATALOG_READ_INDEXES, 7, 1, 0}};
    for (size_t i = 0; i < 6; i++) {
        assert(sc_arena_reset(&tmp) && catalog_sql(kinds[i], &tmp, &sql) && sql.len > 20);
    }
}

/* Plan U18 step 1: the query link down or failed shows the tree greyed and
 * Disconnected, whatever the metadata link does; with it up, a failed
 * metadata link is an error with its own Retry. */
static void sidebar_states(void) {
    catalog c;
    assert(sc_arena_reset(&rec) && sc_arena_reset(&keep) && catalog_init(&c, &rec, &keep));
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
    assert(sc_arena_reset(&rec) && sc_arena_reset(&keep) && catalog_init(&c, &rec, &keep));
    assert(catalog_restart(&c));
    result r = build(5, 1, session_row);
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
    quote_ident();
    session_and_schemas();
    relation_kinds();
    columns_and_detail();
    wrong_shape();
    failures();
    restart_keeps_expansion();
    sql_text();
    sidebar_states();
    full_queue();
    puts("explorer_catalog_test: ok");
    return 0;
}
