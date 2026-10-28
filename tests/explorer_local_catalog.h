#ifndef EXPLORER_LOCAL_CATALOG_TEST_H
#define EXPLORER_LOCAL_CATALOG_TEST_H

#include "../examples/explorer/meta.h"
#include "../examples/explorer/frame.c"
#include "../examples/explorer/nav.c"

static const char local_catalog_sql[] =
    "CREATE TABLE pages(n INTEGER PRIMARY KEY);"
    "WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n<2500) "
    "INSERT INTO pages SELECT n FROM seq;"
    "CREATE TABLE \"quote'\"\";日本\"(\"b\"\"x\" TEXT NOT NULL,a INTEGER NOT NULL,PRIMARY KEY(a,\"b\"\"x\"));"
    "INSERT INTO \"quote'\"\";日本\" VALUES ('five',5),('seventeen',17);"
    "CREATE VIEW slow_count AS WITH RECURSIVE seq(n) AS "
    "(SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n<1000000000) SELECT n FROM seq;";

static char *local_long_definition(void) {
    const char *prefix = "CREATE VIEW long_view AS SELECT '";
    const char *suffix = "' AS payload";
    size_t n = strlen(prefix);
    char *sql = malloc(n + 5001 + strlen(suffix) + 1);
    assert(sql != NULL);
    memcpy(sql, prefix, n);
    memset(sql + n, 'z', 5001);
    strcpy(sql + n + 5001, suffix);
    return sql;
}

static alignas(8) uint8_t local_records[2u << 20], local_keep[16384], local_defs[1u << 20];
static alignas(8) uint8_t local_sql_mem[128u << 10], local_nav_mem[NAV_NAMES_BYTES + 16384];

static void local_close_wait(void *arg, uint32_t ms) {
    (void)arg;
    (void)sc_wake_wait(bell, ms);
    (void)sc_wake_drain(bell);
}

static void local_wait(meta_side *m) {
    uint64_t end = sc_clock_ms() + 15000;
    for (;;) {
        conn_link *q = links_cur(m->links);
        (void)link_poll(q);
        (void)meta_poll(m);
        links_drain(m->links);
        if (m->conn != 0 && !m->running && m->cat.queued == 0) break;
        if (sc_clock_ms() > end || q->broken || m->cat.schemas_load == CATALOG_LOAD_FAILED) {
            fprintf(stderr, "local catalog wait failed: %.*s\n", (int)m->cat.error.len, m->cat.error.ptr);
            abort();
        }
        (void)sc_wake_wait(bell, 5);
        (void)sc_wake_drain(bell);
    }
    assert(m->cat.schemas_load == CATALOG_LOAD_DONE);
}

static catalog_relation local_relation(catalog *c, const char *schema, const char *name,
                                       catalog_schema *out) {
    catalog_schema s;
    catalog_relation r;
    for (size_t i = 0; catalog_schema_at(c, i, &s); i++) {
        if (!sc_str_eq_lit(c->arena, s.name, (const uint8_t *)schema, strlen(schema))) continue;
        for (size_t j = 0; catalog_relation_at(c, &s, j, &r); j++) {
            if (sc_str_eq_lit(c->arena, r.name, (const uint8_t *)name, strlen(name))) {
                *out = s;
                return r;
            }
        }
    }
    fprintf(stderr, "local relation missing: %s.%s\n", schema, name);
    abort();
}

static void local_read_details(meta_side *m, const char *name) {
    catalog_schema s;
    catalog_relation r = local_relation(&m->cat, "main", name, &s);
    assert(catalog_want_detail(&m->cat, s.oid, r.oid));
    local_wait(m);
    r = local_relation(&m->cat, "main", name, &s);
    if (r.detail != CATALOG_LOAD_DONE) {
        fprintf(stderr, "local detail %s failed: %.*s\n", name, (int)r.error.len, r.error.ptr);
        abort();
    }
}

static void local_page(meta_side *m, const char *name, uint64_t page, uint64_t rows,
                        const char *first, const char *last, bool unordered) {
    catalog_schema s;
    catalog_relation r = local_relation(&m->cat, "main", name, &s);
    sc_str sql;
    bool note = !unordered;
    assert(sc_arena_reset(m->sql) && page_sql(&m->cat, s.oid, r.oid, page, PAGE_SIZE, m->sql, &sql, &note));
    assert(note == unordered);
    char *copy = malloc(sql.len + 1);
    assert(copy != NULL);
    memcpy(copy, sql.ptr, sql.len);
    copy[sql.len] = 0;
    assert(strstr(copy, "ctid") == NULL && strstr(copy, "rowid") == NULL);
    free(copy);
    conn_link *q = links_cur(m->links);
    assert(link_run_limit(q, m->sql, sql, PAGE_SIZE));
    WAIT(q, !conn_ui_active(&q->ui), 10000);
    assert(q->ui.q == Q_ROWS && q->res.rows == rows);
    if (rows > 0) {
        cell(q, 0, 0, first);
        cell(q, rows - 1, 0, last);
    }
}

static void local_catalog(sc_db_engine engine, void (*alter)(void)) {
    sc_arena records, keep, defs, sql, names;
    sc_arena_init(&records, local_records, sizeof local_records);
    sc_arena_init(&keep, local_keep, sizeof local_keep);
    sc_arena_init(&defs, local_defs, sizeof local_defs);
    sc_arena_init(&sql, local_sql_mem, sizeof local_sql_mem);
    sc_arena_init(&names, local_nav_mem, sizeof local_nav_mem);
    link_set set;
    conn_limits limits = {4, 32, 10000};
    links_init(&set, &pool, limits, ring, &bell, &text, str(path));
    set.engine = engine;
    assert(links_open(&set));
    meta_side m;
    assert(meta_init(&m, &set, &records, &keep, &defs, &sql));
    local_wait(&m);
    assert(m.cat.engine == engine && m.cat.session.loaded);
    for (size_t i = 0; i < catalog_schema_count(&m.cat); i++) assert(catalog_expand(&m.cat, i));
    local_wait(&m);
    catalog_schema s;
    for (size_t i = 0; catalog_schema_at(&m.cat, i, &s); i++) assert(sc_tslice_len(s.functions) == 0);
    assert(catalog_schema_count(&m.cat) == (engine == SC_DB_SQLITE ? 1u : 2u));
    local_read_details(&m, "parent");
    local_read_details(&m, "child");
    local_read_details(&m, "pages");
    local_read_details(&m, "sample_view");
    local_read_details(&m, "quote'\";日本");
    catalog_relation parent = local_relation(&m.cat, "main", "parent", &s);
    catalog_column a, b, g;
    assert(catalog_column_at(&m.cat, &parent, 0, &a) && a.key_pos == 2 && a.not_null);
    assert(catalog_column_at(&m.cat, &parent, 1, &b) && b.key_pos == 1 && b.not_null);
    catalog_relation child = local_relation(&m.cat, "main", "child", &s);
    assert(catalog_column_at(&m.cat, &child, 2, &g) && g.generated &&
        g.generated_virtual == (engine == SC_DB_DUCKDB));
    bool foreign = false;
    catalog_constraint k;
    for (size_t i = 0; catalog_constraint_at(&m.cat, &child, i, &k); i++) {
        if (k.type != 'f') continue;
        uint32_t one = 0, two = 0;
        assert(SC_TSLICE_GET(uint32_t, m.cat.arena, k.columns, 0, &one) && one == 2);
        assert(SC_TSLICE_GET(uint32_t, m.cat.arena, k.columns, 1, &two) && two == 1);
        foreign = true;
    }
    assert(foreign && sc_tslice_len(child.indexes) == 1);
    catalog_relation quoted = local_relation(&m.cat, "main", "quote'\";日本", &s);
    assert(catalog_column_at(&m.cat, &quoted, 0, &b) && b.key_pos == 2);
    assert(catalog_column_at(&m.cat, &quoted, 1, &a) && a.key_pos == 1);
    assert(!page_unordered(&m.cat, &quoted));
    local_page(&m, "pages", 1, 1000, "1", "1000", false);
    local_page(&m, "pages", 2, 1000, "1001", "2000", false);
    local_page(&m, "pages", 3, 500, "2001", "2500", false);
    local_page(&m, "pages", 4, 0, NULL, NULL, false);
    catalog_relation pages = local_relation(&m.cat, "main", "pages", &s);
    assert(catalog_want_count(&m.cat, s.oid, pages.oid));
    local_wait(&m);
    pages = local_relation(&m.cat, "main", "pages", &s);
    assert(pages.count_load == CATALOG_LOAD_DONE && pages.count == 2500);
    catalog_relation slow = local_relation(&m.cat, "main", "slow_count", &s);
    uint64_t started = sc_clock_ms();
    assert(catalog_want_count(&m.cat, s.oid, slow.oid));
    local_wait(&m);
    slow = local_relation(&m.cat, "main", "slow_count", &s);
    assert(slow.count_load == CATALOG_LOAD_FAILED && sc_clock_ms() - started < 6000);
    local_page(&m, "pages", 3, 500, "2001", "2500", false);
    catalog_relation long_view = local_relation(&m.cat, "main", "long_view", &s);
    assert(catalog_want_relation_definition(&m.cat, s.oid, long_view.oid));
    local_wait(&m);
    uint32_t oid;
    bool ok;
    sc_str definition;
    assert(catalog_take_definition(&m.cat, &oid, &ok, &definition) && ok && oid == long_view.oid && definition.len > 5001);
    size_t longest = 0, run = 0;
    for (size_t i = 0; i < definition.len; i++) {
        uint8_t byte = 0;
        assert(sc_slice_get(m.cat.defs, definition, i, &byte));
        run = byte == 'z' ? run + 1 : 0;
        if (run > longest) longest = run;
    }
    assert(longest == 5001);
    if (engine == SC_DB_SQLITE) {
        local_read_details(&m, "nullable_pk");
        local_read_details(&m, "desc_pk");
        catalog_relation nullable = local_relation(&m.cat, "main", "nullable_pk", &s);
        assert(page_unordered(&m.cat, &nullable));
        assert(catalog_column_at(&m.cat, &nullable, 0, &a) && a.key_pos == 2 && !a.not_null);
        catalog_relation descending = local_relation(&m.cat, "main", "desc_pk", &s);
        assert(page_unordered(&m.cat, &descending));
    } else {
        catalog_relation other = local_relation(&m.cat, "other", "sample", &s);
        assert(page_unordered(&m.cat, &other));
    }
    nav_stack nav;
    assert(nav_init(&nav, &names));
    quoted = local_relation(&m.cat, "main", "quote'\";日本", &s);
    assert(nav_open(&nav, &m.cat, s.oid, quoted.oid, page_total_open(&quoted)));
    pages = local_relation(&m.cat, "main", "pages", &s);
    assert(nav_open(&nav, &m.cat, s.oid, pages.oid, page_total_open(&pages)));
    assert(links_close(&set, 5000, local_close_wait, NULL));
    alter();
    links_init(&set, &pool, limits, ring, &bell, &text, str(path));
    set.engine = engine;
    m.conn = 0;
    assert(links_open(&set));
    local_wait(&m);
    nav_resolve(&nav, &m.cat);
    local_wait(&m);
    nav_resolve(&nav, &m.cat);
    nav_entry entry;
    assert(nav_current(&nav, &entry) && entry.gen == m.cat.gen && !entry.gone && entry.oid != pages.oid);
    catalog_relation resolved;
    assert(catalog_relation_of(&m.cat, entry.schema, entry.oid, &s, &resolved) &&
        SC_STR_EQ_LIT(m.cat.arena, resolved.name, "pages"));
    assert(nav_back(&nav) && nav_current(&nav, &entry) && entry.gone && entry.oid == 0);
    assert(links_close(&set, 5000, local_close_wait, NULL));
    puts("ok local native catalog, structure, key order, quoted names, 1000/1000/500/0 pages, count deadline, long definition and reload identity");
}

#endif
