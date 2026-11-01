#include "page.h"
#include "engine_policy.h"

static sc_str page_empty(const sc_arena *a) {
    sc_str s = {NULL, 0, a->epoch};
    return s;
}

page_total page_total_open(const catalog_relation *r) {
    page_total t = {r->estimate_known, r->estimate, PAGE_COUNT_PENDING, 0, PAGE_SIZE};
    return t;
}

page_total page_total_update(page_total t, const catalog *c, uint32_t schema, uint32_t oid) {
    catalog_schema s;
    catalog_relation r;
    if (t.count_state != PAGE_COUNT_PENDING || !catalog_relation_of(c, schema, oid, &s, &r)) {
        return t;
    }
    if (r.count_load == CATALOG_LOAD_DONE) {
        t.count_state = PAGE_COUNT_KNOWN;
        t.count = r.count;
    } else if (r.count_load == CATALOG_LOAD_FAILED) {
        t.count_state = PAGE_COUNT_TIMED_OUT;
    }
    return t;
}

bool page_unordered(const catalog *c, const catalog_relation *r) {
    if (r->kind == CATALOG_REL_VIEW || r->kind == CATALOG_REL_FOREIGN) return true;
    if (!engine_policy_of(c->engine).unordered_needs_key) return false;
    bool key = false;
    catalog_column col;
    for (size_t i = 0; catalog_column_at(c, r, i, &col); i++) {
        if (col.key_pos > 0) {
            if (!col.not_null) return true;
            key = true;
        }
    }
    return !key;
}

bool page_ready(const catalog *c, uint32_t schema, uint32_t oid) {
    catalog_schema s;
    catalog_relation r;
    if (!catalog_relation_of(c, schema, oid, &s, &r)) {
        return false;
    }
    return r.kind == CATALOG_REL_VIEW || r.kind == CATALOG_REL_FOREIGN ||
           r.columns_loaded || r.detail == CATALOG_LOAD_FAILED;
}

static bool page_ident(sc_arena *a, sc_buf *b, const sc_arena *sa, sc_str name) {
    sc_str q;
    return catalog_quote_ident(sa, name, a, &q) && sc_buf_str(a, b, a, q);
}

static bool page_from(sc_arena *a, sc_buf *b, const catalog *c, const catalog_schema *s,
                      const catalog_relation *r) {
    return SC_BUF_LIT(a, b, " FROM ") &&
           (c->engine != SC_DB_DUCKDB ||
            (page_ident(a, b, c->arena, c->session.database) && SC_BUF_LIT(a, b, "."))) &&
           page_ident(a, b, c->arena, s->name) &&
           SC_BUF_LIT(a, b, ".") && page_ident(a, b, c->arena, r->name);
}

static size_t page_room(const catalog *c, const catalog_schema *s, const catalog_relation *r) {
    size_t n = 2u * s->name.len + 2u * r->name.len + 128u;
    if (c->engine == SC_DB_DUCKDB) n += 2u * c->session.database.len + 8u;
    catalog_column col;
    for (size_t i = 0; catalog_column_at(c, r, i, &col); i++) {
        if (col.key_pos > 0 && n < SIZE_MAX / 2u - 2u * col.name.len - 8u) {
            n += 2u * col.name.len + 8u;
        }
    }
    return n;
}

static bool page_key(sc_arena *a, sc_buf *b, const catalog *c, const catalog_relation *r, bool *any) {
    *any = false;
    bool ok = true;
    for (uint32_t pos = 1; ok; pos++) {
        catalog_column col;
        bool found = false;
        for (size_t i = 0; !found && catalog_column_at(c, r, i, &col); i++) {
            found = col.key_pos == pos;
        }
        if (!found) {
            break;
        }
        ok = (*any ? SC_BUF_LIT(a, b, ", ") : SC_BUF_LIT(a, b, " ORDER BY ")) &&
             page_ident(a, b, c->arena, col.name);
        *any = true;
    }
    return ok;
}

bool page_sql(const catalog *c, uint32_t schema, uint32_t oid, uint64_t page, uint32_t size, sc_arena *a,
              sc_str *out, bool *note) {
    catalog_schema s;
    catalog_relation r;
    if (size < 100u || size > 10000u || page == 0 || page - 1u > UINT64_MAX / size ||
        !catalog_relation_of(c, schema, oid, &s, &r)) {
        return false;
    }
    bool unordered = page_unordered(c, &r);
    bool key = false;
    sc_buf b = sc_buf_new(sc_arena_alloc(a, page_room(c, &s, &r)));
    bool ok = SC_BUF_LIT(a, &b, "SELECT *") && page_from(a, &b, c, &s, &r) &&
              (unordered || page_key(a, &b, c, &r, &key));
    if (ok && !unordered && !key) {
        ok = r.kind == CATALOG_REL_PARTITIONED ? SC_BUF_LIT(a, &b, " ORDER BY tableoid, ctid")
                                               : SC_BUF_LIT(a, &b, " ORDER BY ctid");
    }
    ok = ok && SC_BUF_LIT(a, &b, " LIMIT ") && sc_buf_u64(a, &b, size) &&
         SC_BUF_LIT(a, &b, " OFFSET ") && sc_buf_u64(a, &b, (page - 1u) * size);
    if (!ok) {
        return false;
    }
    *out = sc_buf_view(a, b);
    *note = unordered;
    return true;
}

void page_open(catalog *c, uint32_t schema, uint32_t oid) {
    (void)catalog_want_detail(c, schema, oid);
    (void)catalog_want_count(c, schema, oid);
}

void page_rewant(catalog *c, uint32_t schema, uint32_t oid, page_total t, bool count) {
    catalog_schema s;
    catalog_relation r;
    if (!catalog_relation_of(c, schema, oid, &s, &r)) {
        return;
    }
    if (r.detail == CATALOG_LOAD_NONE) {
        (void)catalog_want_detail(c, schema, oid);
    }
    if (count && t.count_state == PAGE_COUNT_PENDING && r.count_load == CATALOG_LOAD_NONE) {
        (void)catalog_want_count(c, schema, oid);
    }
}

bool page_gone(const catalog *c, uint32_t schema, uint32_t oid) {
    catalog_schema s;
    catalog_relation r;
    if (c->schemas_load != CATALOG_LOAD_DONE || catalog_relation_of(c, schema, oid, &s, &r)) {
        return false;
    }
    for (size_t i = 0; catalog_schema_at(c, i, &s); i++) {
        if (s.oid == schema) {
            return s.load == CATALOG_LOAD_DONE;
        }
    }
    return true;
}

static bool page_rows_total(page_total t, uint64_t *rows, bool *about) {
    if (t.count_state == PAGE_COUNT_KNOWN) {
        *rows = t.count;
        *about = false;
        return true;
    }
    *rows = t.estimate;
    *about = true;
    return t.count_state == PAGE_COUNT_PENDING && t.estimate_known;
}

static sc_str page_done(sc_arena *a, sc_buf b, bool ok, bool *lost) {
    if (!ok) {
        *lost = true;
        return page_empty(a);
    }
    return sc_buf_view(a, b);
}

sc_str page_label(sc_arena *a, uint64_t page, page_total t, bool *lost) {
    uint64_t rows = 0;
    uint32_t size = t.size > 0 ? t.size : PAGE_SIZE;
    bool about = false;
    sc_buf b = sc_buf_new(sc_arena_alloc(a, 64));
    bool ok = SC_BUF_LIT(a, &b, "Page ") && sc_buf_u64_sep(a, &b, page);
    if (ok && page_rows_total(t, &rows, &about)) {
        uint64_t pages = rows / size + (rows % size != 0 ? 1u : 0u);
        ok = SC_BUF_LIT(a, &b, " of ") && (!about || SC_BUF_LIT(a, &b, "~")) &&
             sc_buf_u64_sep(a, &b, pages > 0 ? pages : 1u);
    }
    return page_done(a, b, ok, lost);
}

sc_str page_rows_label(sc_arena *a, uint64_t page, uint64_t rows, page_total t, bool capped,
                       bool *lost) {
    sc_buf b = sc_buf_new(sc_arena_alloc(a, 96));
    if (rows == 0) {
        return page_done(a, b, SC_BUF_LIT(a, &b, "No rows on this page"), lost);
    }
    uint32_t size = t.size > 0 ? t.size : PAGE_SIZE;
    uint64_t first = page > 0 && page - 1u <= (UINT64_MAX - size) / size ? (page - 1u) * size : 0u;
    uint64_t last = rows - 1u <= UINT64_MAX - first - 1u ? first + rows : UINT64_MAX;
    uint64_t total = 0;
    bool about = false;
    bool ok = SC_BUF_LIT(a, &b, "Rows ") && sc_buf_u64_sep(a, &b, first + 1u) &&
              SC_BUF_LIT(a, &b, "\xE2\x80\x93") && sc_buf_u64_sep(a, &b, last);
    if (ok && page_rows_total(t, &total, &about)) {
        ok = SC_BUF_LIT(a, &b, " of ") && (!about || SC_BUF_LIT(a, &b, "~")) && sc_buf_u64_sep(a, &b, total);
    }
    ok = ok && (!capped || SC_BUF_LIT(a, &b, " (memory limit)"));
    return page_done(a, b, ok, lost);
}

bool page_can_prev(uint64_t page) {
    return page > 1u;
}

bool page_can_next(bool ended, uint64_t rows, bool capped, uint32_t size) {
    return ended && !capped && rows >= (size > 0 ? size : PAGE_SIZE);
}


static bool page_listed(const catalog_constraint *k) {
    return k->type == 'c' || k->type == 'f';
}

static size_t page_listed_count(const catalog *c, const catalog_relation *r) {
    size_t n = 0;
    catalog_constraint k;
    for (size_t i = 0; catalog_constraint_at(c, r, i, &k); i++) {
        n += page_listed(&k) ? 1u : 0u;
    }
    return n;
}

size_t page_structure_count(const catalog *c, const catalog_relation *r) {
    return 3u + sc_tslice_len(r->columns) + sc_tslice_len(r->indexes) + page_listed_count(c, r);
}

static bool page_names(const catalog *c, const catalog_constraint *k, uint32_t num, bool alone) {
    size_t n = sc_tslice_len(k->columns);
    if (alone && n != 1u) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        uint32_t x = 0;
        if (SC_TSLICE_GET(uint32_t, c->arena, k->columns, i, &x) && x == num) {
            return true;
        }
    }
    return false;
}

static void page_column_row(const catalog *c, const catalog_relation *r, const catalog_column *col,
                            page_row *p) {
    p->kind = PAGE_ROW_COLUMN;
    p->name = col->name;
    p->type = col->type;
    p->detail = col->default_expr;
    p->chips = (col->key_pos > 0 ? PAGE_CHIP_PRIMARY_KEY : 0u) | (col->not_null ? PAGE_CHIP_NOT_NULL : 0u) |
               (col->identity != CATALOG_IDENTITY_NONE ? PAGE_CHIP_IDENTITY : 0u) |
               (col->generated ? PAGE_CHIP_GENERATED : 0u) |
               (col->hidden ? PAGE_CHIP_HIDDEN : 0u) |
               (col->generated_virtual && engine_policy_of(c->engine).file_target ? PAGE_CHIP_VIRTUAL : 0u);
    catalog_constraint k;
    for (size_t i = 0; catalog_constraint_at(c, r, i, &k); i++) {
        if (k.type == 'u' && page_names(c, &k, col->num, true)) {
            p->chips |= PAGE_CHIP_UNIQUE;
        }
        if (k.type == 'f' && (p->chips & PAGE_CHIP_FOREIGN) == 0 && page_names(c, &k, col->num, false)) {
            p->chips |= PAGE_CHIP_FOREIGN;
            p->target = k.target;
        }
    }
}

bool page_structure_row(const catalog *c, const catalog_relation *r, size_t i, sc_arena *a,
                        page_row *out) {
    page_row p;
    p.name = page_empty(c->arena);
    p.type = p.name;
    p.detail = p.name;
    p.target = p.name;
    p.chips = 0;
    size_t cols = sc_tslice_len(r->columns), idx = sc_tslice_len(r->indexes);
    catalog_column col;
    catalog_index x;
    catalog_constraint k;
    bool ok = true;
    if (i == 0 || i == cols + 1u || i == cols + idx + 2u) {
        p.kind = PAGE_ROW_SECTION;
        if (i == 0) p.name = SC_STR_LIT(a, "Columns");
        else if (i == cols + 1u) p.name = SC_STR_LIT(a, "Indexes");
        else if (engine_policy_of(c->engine).partial_constraints)
            p.name = SC_STR_LIT(a, "Constraints (partial; see Definition)");
        else p.name = SC_STR_LIT(a, "Constraints");
        ok = p.name.len > 0;
    } else if (i <= cols) {
        ok = catalog_column_at(c, r, i - 1u, &col);
        if (ok) {
            page_column_row(c, r, &col, &p);
        }
    } else if (i <= cols + idx + 1u) {
        ok = catalog_index_at(c, r, i - cols - 2u, &x);
        if (ok) {
            p.kind = PAGE_ROW_INDEX;
            p.name = x.name;
            p.detail = x.def;
        }
    } else {
        size_t want = i - cols - idx - 3u, seen = 0;
        ok = false;
        for (size_t j = 0; !ok && catalog_constraint_at(c, r, j, &k); j++) {
            if (page_listed(&k)) {
                ok = seen == want;
                seen++;
            }
        }
        if (ok) {
            p.kind = PAGE_ROW_CONSTRAINT;
            p.name = k.name;
            p.detail = k.def;
        }
    }
    if (!ok) {
        return false;
    }
    *out = p;
    return true;
}
