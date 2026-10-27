#include "catalog.h"
#include "safec/utf8.h"

#define CAT_SQL_BYTES 1024u

static sc_str cat_empty(const sc_arena *a) {
    sc_str s = {NULL, 0, a->epoch};
    return s;
}

static bool cat_line_char(sc_utf8_char ch) {
    return ch.valid && ch.len > 0 &&
           (ch.cp == '\t' || (ch.cp >= 0x20u && ch.cp != 0x7fu && (ch.cp < 0x80u || ch.cp > 0x9fu)));
}

static bool cat_one_line(const sc_arena *sa, sc_str s) {
    for (size_t i = 0; i < s.len;) {
        sc_utf8_char ch = sc_utf8_next(sa, s, i);
        if (!cat_line_char(ch)) {
            return false;
        }
        i += ch.len;
    }
    return true;
}

/* A copy of `s` (live in `sa`) in `a`, with an ellipsis after it when
 * `cut`. */
static bool cat_copy(sc_arena *a, const sc_arena *sa, sc_str s, bool cut, sc_str *out) {
    size_t extra = cut ? 3u : 0u;
    if (s.len == 0 && !cut) {
        *out = cat_empty(a);
        return true;
    }
    if (s.len > SIZE_MAX - extra) {
        return false;
    }
    sc_buf b = sc_buf_new(sc_arena_alloc(a, s.len + extra));
    if (!sc_buf_str(a, &b, sa, s) || (cut && !SC_BUF_LIT(a, &b, "\xE2\x80\xA6"))) {
        return false;
    }
    *out = sc_buf_view(a, b);
    return true;
}

static bool cat_text(catalog *c, const sc_arena *sa, proto_cell cell, sc_str *out) {
    if (cell.null) {
        return false;
    }
    bool ok = cat_copy(c->arena, sa, cell.bytes, cell.truncated, out);
    c->full = c->full || !ok;
    return ok;
}

/* Whether `list` got its `n` records; when not, the arena is full. */
static bool cat_fits(catalog *c, sc_tslice list, uint64_t n) {
    bool ok = sc_tslice_len(list) == n;
    c->full = c->full || !ok;
    return ok;
}

static bool cat_text_or_null(catalog *c, const sc_arena *sa, proto_cell cell, sc_str *out) {
    if (cell.null) {
        *out = cat_empty(c->arena);
        return true;
    }
    return cat_text(c, sa, cell, out);
}

static bool cat_u64(const sc_arena *sa, proto_cell cell, uint64_t max, uint64_t *out) {
    if (cell.null || cell.truncated || cell.bytes.len == 0) {
        return false;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < cell.bytes.len; i++) {
        uint8_t ch = 0;
        if (!sc_slice_get(sa, cell.bytes, i, &ch) || ch < '0' || ch > '9') {
            return false;
        }
        uint64_t d = (uint64_t)(ch - '0');
        if (v > (max - d) / 10u) {
            return false;
        }
        v = v * 10u + d;
    }
    *out = v;
    return true;
}

static bool cat_u32(const sc_arena *sa, proto_cell cell, uint32_t *out) {
    uint64_t v = 0;
    if (!cat_u64(sa, cell, UINT32_MAX, &v)) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

/* One byte, or none when `none_ok` (a "char" column's empty value). */
static bool cat_byte(const sc_arena *sa, proto_cell cell, bool none_ok, uint8_t *out) {
    if (cell.null || cell.bytes.len > 1u || (cell.bytes.len == 0 && !none_ok)) {
        return false;
    }
    *out = 0;
    return cell.bytes.len == 0 || sc_slice_get(sa, cell.bytes, 0, out);
}

static bool cat_bool(const sc_arena *sa, proto_cell cell, bool *out) {
    uint8_t b = 0;
    if (!cat_byte(sa, cell, false, &b) || (b != 't' && b != 'f')) {
        return false;
    }
    *out = b == 't';
    return true;
}

/* reltuples as int8: -1 is unknown (never vacuumed or analysed, PG 14+). */
static bool cat_estimate(const sc_arena *sa, proto_cell cell, bool *known, uint64_t *rows) {
    if (sc_str_eq_lit(sa, cell.bytes, (const uint8_t *)"-1", 2) && !cell.null) {
        *known = false;
        *rows = 0;
        return true;
    }
    *known = true;
    return cat_u64(sa, cell, UINT64_MAX, rows);
}

static bool cat_push(catalog *c, catalog_read_kind kind, uint32_t oid, uint32_t schema) {
    size_t cap = sc_tslice_len(c->queue);
    if (c->queued >= cap) {
        return false;
    }
    catalog_read r = {kind, oid, schema, c->gen};
    size_t at = (c->head + c->queued) % cap;
    if (!SC_TSLICE_SET(catalog_read, c->arena, c->queue, at, &r)) {
        return false;
    }
    c->queued++;
    return true;
}

static bool cat_room(const catalog *c, size_t n) {
    size_t cap = sc_tslice_len(c->queue);
    return c->queued <= cap && n <= cap - c->queued;
}

bool catalog_next(catalog *c, catalog_read *out) {
    size_t cap = sc_tslice_len(c->queue);
    if (c->queued == 0 || cap == 0 ||
        !SC_TSLICE_GET(catalog_read, c->arena, c->queue, c->head, out)) {
        return false;
    }
    c->head = (c->head + 1u) % cap;
    c->queued--;
    return true;
}

static sc_tslice cat_none(void) {
    sc_tslice none = {{NULL, 0, 0}, 0};
    return none;
}

static void cat_blank_session(catalog *c) {
    catalog_session blank = {0};
    c->session = blank;
    c->session.version = cat_empty(c->arena);
    c->session.database = cat_empty(c->arena);
    c->session.user = cat_empty(c->arena);
}

bool catalog_init(catalog *c, sc_arena *a, sc_arena *keep, sc_arena *defs) {
    catalog z = {0};
    *c = z;
    c->arena = a;
    c->keep = keep;
    c->defs = defs;
    c->kept = cat_none();
    c->schemas = cat_none();
    c->queue = cat_none();
    c->error = cat_empty(a);
    c->room_error = cat_empty(a);
    c->def = cat_empty(defs);
    cat_blank_session(c);
    return sc_arena_reset(a) && sc_arena_reset(keep) && sc_arena_reset(defs);
}

size_t catalog_schema_count(const catalog *c) {
    return sc_tslice_len(c->schemas);
}

bool catalog_schema_at(const catalog *c, size_t i, catalog_schema *out) {
    return SC_TSLICE_GET(catalog_schema, c->arena, c->schemas, i, out);
}

static bool cat_put_schema(catalog *c, size_t i, const catalog_schema *s) {
    return SC_TSLICE_SET(catalog_schema, c->arena, c->schemas, i, s);
}

static bool cat_keep_names(catalog *c) {
    size_t n = 0;
    catalog_schema s;
    if (c->schemas_load != CATALOG_LOAD_DONE) {
        return true;
    }
    for (size_t i = 0; catalog_schema_at(c, i, &s); i++) {
        n += s.expanded ? 1u : 0u;
    }
    c->kept_count = 0;
    if (!sc_arena_reset(c->keep)) {
        return false;
    }
    c->kept = n > 0 ? SC_TSLICE_ALLOC(sc_str, c->keep, n) : cat_none();
    for (size_t i = 0; sc_tslice_len(c->kept) == n && catalog_schema_at(c, i, &s); i++) {
        sc_str name;
        if (s.expanded && cat_copy(c->keep, c->arena, s.name, false, &name) &&
            SC_TSLICE_SET(sc_str, c->keep, c->kept, c->kept_count, &name)) {
            c->kept_count++;
        }
    }
    return true;
}

bool catalog_restart(catalog *c) {
    if (!cat_keep_names(c) || !sc_arena_reset(c->arena)) {
        return false;
    }
    c->gen++;
    cat_blank_session(c);
    c->schemas = cat_none();
    c->error = cat_empty(c->arena);
    c->def_load = CATALOG_LOAD_NONE;
    c->def_oid = 0;
    c->def = cat_empty(c->defs);
    c->room_error = SC_STR_LIT(c->arena, "The sidebar has no room for more catalog records; reload.");
    c->head = 0;
    c->queued = 0;
    c->queue = SC_TSLICE_ALLOC(catalog_read, c->arena, CATALOG_QUEUE);
    c->schemas_load = CATALOG_LOAD_PENDING;
    return sc_tslice_len(c->queue) == CATALOG_QUEUE && cat_push(c, CATALOG_READ_TIMEOUT, 0, 0) &&
           cat_push(c, CATALOG_READ_SESSION, 0, 0) && cat_push(c, CATALOG_READ_SCHEMAS, 0, 0);
}

typedef struct {
    sc_arena *a;
    sc_buf b;
    bool ok;
} cat_sql_text;

static void cat_lit(cat_sql_text *t, const uint8_t *lit, size_t n) {
    t->ok = t->ok && sc_buf_lit(t->a, &t->b, lit, n);
}
#define CAT_LIT(t, lit) cat_lit((t), (const uint8_t *)("" lit), sizeof("" lit) - 1)

static void cat_oid(cat_sql_text *t, uint32_t oid) {
    t->ok = t->ok && sc_buf_u64(t->a, &t->b, oid);
}

bool catalog_sql(catalog_read r, sc_arena *a, sc_str *out) {
    cat_sql_text t = {a, sc_buf_new(sc_arena_alloc(a, CAT_SQL_BYTES)), true};
    switch (r.kind) {
    case CATALOG_READ_TIMEOUT:
        CAT_LIT(&t, "SET statement_timeout = '10s'");
        break;
    case CATALOG_READ_SESSION:
        CAT_LIT(&t, "SELECT pg_catalog.current_setting('server_version_num'), "
                    "pg_catalog.current_setting('server_version'), "
                    "pg_catalog.current_setting('standard_conforming_strings'), "
                    "pg_catalog.current_database(), current_user");
        break;
    case CATALOG_READ_SCHEMAS:
        CAT_LIT(&t, "SELECT n.oid, n.nspname FROM pg_catalog.pg_namespace n "
                    "WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema' ORDER BY 2");
        break;
    case CATALOG_READ_RELATIONS:
        CAT_LIT(&t, "SELECT c.oid, c.relname, c.relkind, c.reltuples::pg_catalog.int8 "
                    "FROM pg_catalog.pg_class c WHERE c.relnamespace = ");
        cat_oid(&t, r.oid);
        CAT_LIT(&t, "::pg_catalog.oid AND c.relkind IN ('r', 'p', 'v', 'm', 'f') "
                    "AND NOT c.relispartition ORDER BY 2");
        break;
    case CATALOG_READ_FUNCTIONS:
        CAT_LIT(&t, "SELECT p.oid, p.proname, p.prokind, "
                    "pg_catalog.pg_get_function_identity_arguments(p.oid), "
                    "pg_catalog.pg_get_function_result(p.oid) "
                    "FROM pg_catalog.pg_proc p WHERE p.pronamespace = ");
        cat_oid(&t, r.oid);
        CAT_LIT(&t, "::pg_catalog.oid AND p.prokind <> 'a' ORDER BY 2, 4");
        break;
    case CATALOG_READ_COLUMNS:
        CAT_LIT(&t, "SELECT a.attnum, a.attname, pg_catalog.format_type(a.atttypid, a.atttypmod), "
                    "a.attnotnull, pg_catalog.pg_get_expr(d.adbin, d.adrelid), a.attidentity, "
                    "a.attgenerated, COALESCE(pg_catalog.array_position(k.conkey, a.attnum), 0) "
                    "FROM pg_catalog.pg_attribute a "
                    "LEFT JOIN pg_catalog.pg_attrdef d ON d.adrelid = a.attrelid AND d.adnum = a.attnum "
                    "LEFT JOIN pg_catalog.pg_constraint k ON k.conrelid = a.attrelid AND k.contype = 'p' "
                    "WHERE a.attrelid = ");
        cat_oid(&t, r.oid);
        CAT_LIT(&t, "::pg_catalog.oid AND a.attnum > 0 AND NOT a.attisdropped ORDER BY a.attnum");
        break;
    case CATALOG_READ_CONSTRAINTS:
        CAT_LIT(&t, "SELECT r.conname, r.contype, pg_catalog.pg_get_constraintdef(r.oid, true), "
                    "COALESCE(pg_catalog.array_to_string(r.conkey, ' '), ''), "
                    "CASE WHEN r.contype = 'f' "
                    "THEN r.confrelid::pg_catalog.regclass::pg_catalog.text END "
                    "FROM pg_catalog.pg_constraint r WHERE r.conrelid = ");
        cat_oid(&t, r.oid);
        CAT_LIT(&t, "::pg_catalog.oid AND r.contype <> 'n' ORDER BY 1");
        break;
    case CATALOG_READ_INDEXES:
        CAT_LIT(&t, "SELECT c2.relname, i.indisprimary, i.indisunique, "
                    "pg_catalog.pg_get_indexdef(i.indexrelid, 0, true) "
                    "FROM pg_catalog.pg_index i "
                    "JOIN pg_catalog.pg_class c2 ON c2.oid = i.indexrelid WHERE i.indrelid = ");
        cat_oid(&t, r.oid);
        CAT_LIT(&t, "::pg_catalog.oid ORDER BY i.indisprimary DESC, i.indisunique DESC, 1");
        break;
    case CATALOG_READ_DEFINITION:
        CAT_LIT(&t, "SELECT d.line FROM pg_catalog.regexp_split_to_table("
                    "pg_catalog.pg_get_functiondef(");
        cat_oid(&t, r.oid);
        CAT_LIT(&t, "::pg_catalog.oid), E'\\n') WITH ORDINALITY AS d(line, n) ORDER BY d.n");
        break;
    default:
        return false;
    }
    if (!t.ok) {
        return false;
    }
    *out = sc_buf_view(a, t.b);
    return true;
}

static uint64_t cat_cols(catalog_read_kind k) {
    switch (k) {
    case CATALOG_READ_SESSION:
        return 5;
    case CATALOG_READ_SCHEMAS:
        return 2;
    case CATALOG_READ_RELATIONS:
        return 4;
    case CATALOG_READ_FUNCTIONS:
        return 5;
    case CATALOG_READ_COLUMNS:
        return 8;
    case CATALOG_READ_CONSTRAINTS:
        return 5;
    case CATALOG_READ_INDEXES:
        return 4;
    case CATALOG_READ_DEFINITION:
        return 1;
    default:
        return 0;
    }
}

typedef struct {
    const result *res;
    result_cursor k;
    uint64_t row;
} cat_rows;

static bool cat_rows_start(cat_rows *w, const result *res, catalog_read_kind kind) {
    w->res = res;
    w->row = 0;
    return res->has_columns && res->cols == cat_cols(kind) &&
           (res->rows == 0 || result_seek(&w->k, res, 0));
}

static bool cat_rows_next(cat_rows *w) {
    return w->row == 0 || result_next_row(&w->k);
}

static bool cat_cell(cat_rows *w, proto_cell *out) {
    return result_next_cell(&w->k, out);
}

static const sc_arena *cat_cell_arena(const cat_rows *w) {
    return w->k.rd.arena;
}

static bool cat_session_row(catalog *c, cat_rows *w) {
    proto_cell a;
    catalog_session s = c->session;
    bool ok = cat_rows_next(w) && cat_cell(w, &a) && cat_u32(cat_cell_arena(w), a, &s.version_num) &&
              cat_cell(w, &a) && cat_text(c, cat_cell_arena(w), a, &s.version) && cat_cell(w, &a) && !a.null;
    s.standard_strings = ok && sc_str_eq_lit(cat_cell_arena(w), a.bytes, (const uint8_t *)"on", 2);
    ok = ok && (s.standard_strings ||
                sc_str_eq_lit(cat_cell_arena(w), a.bytes, (const uint8_t *)"off", 3)) &&
         cat_cell(w, &a) && cat_text(c, cat_cell_arena(w), a, &s.database) && cat_cell(w, &a) &&
         cat_text(c, cat_cell_arena(w), a, &s.user);
    if (!ok) {
        return false;
    }
    s.loaded = true;
    c->session = s;
    return true;
}

static bool cat_kind(uint8_t b, catalog_rel_kind *out) {
    switch (b) {
    case 'r':
        *out = CATALOG_REL_TABLE;
        return true;
    case 'p':
        *out = CATALOG_REL_PARTITIONED;
        return true;
    case 'v':
        *out = CATALOG_REL_VIEW;
        return true;
    case 'm':
        *out = CATALOG_REL_MATVIEW;
        return true;
    case 'f':
        *out = CATALOG_REL_FOREIGN;
        return true;
    default:
        return false;
    }
}

static bool cat_identity(uint8_t b, catalog_identity *out) {
    *out = b == 'a' ? CATALOG_IDENTITY_ALWAYS
           : b == 'd' ? CATALOG_IDENTITY_BY_DEFAULT
                      : CATALOG_IDENTITY_NONE;
    return b == 0 || b == 'a' || b == 'd';
}

static bool cat_was_expanded(const catalog *c, sc_str name) {
    for (size_t i = 0; i < c->kept_count; i++) {
        sc_str k;
        if (SC_TSLICE_GET(sc_str, c->keep, c->kept, i, &k) && k.len == name.len) {
            bool same = true;
            for (size_t j = 0; same && j < k.len; j++) {
                uint8_t x = 0, y = 0;
                same = sc_slice_get(c->keep, k, j, &x) && sc_slice_get(c->arena, name, j, &y) &&
                       x == y;
            }
            if (same) {
                return true;
            }
        }
    }
    return false;
}

static bool cat_queue_schema(catalog *c, catalog_schema *s) {
    if (!cat_room(c, 2) || !cat_push(c, CATALOG_READ_RELATIONS, s->oid, 0) ||
        !cat_push(c, CATALOG_READ_FUNCTIONS, s->oid, 0)) {
        s->load = CATALOG_LOAD_FAILED;
        s->load_left = 0;
        s->error = SC_STR_LIT(c->arena, "Too many catalog reads are waiting.");
        return false;
    }
    s->load = CATALOG_LOAD_PENDING;
    s->load_left = 2;
    return true;
}

static bool cat_schemas(catalog *c, cat_rows *w) {
    uint64_t n = w->res->rows;
    sc_tslice list = n > 0 ? SC_TSLICE_ALLOC(catalog_schema, c->arena, (size_t)n) : cat_none();
    if (!cat_fits(c, list, n)) {
        return false;
    }
    for (; w->row < n; w->row++) {
        proto_cell a;
        catalog_schema s = {0};
        s.relations = cat_none();
        s.functions = cat_none();
        s.error = cat_empty(c->arena);
        if (!cat_rows_next(w) || !cat_cell(w, &a) || !cat_u32(cat_cell_arena(w), a, &s.oid) ||
            !cat_cell(w, &a) || !cat_text(c, cat_cell_arena(w), a, &s.name) ||
            !SC_TSLICE_SET(catalog_schema, c->arena, list, (size_t)w->row, &s)) {
            return false;
        }
    }
    c->schemas = list;
    for (size_t i = 0; i < (size_t)n; i++) {
        catalog_schema s;
        if (catalog_schema_at(c, i, &s) && cat_was_expanded(c, s.name)) {
            s.expanded = true;
            (void)cat_queue_schema(c, &s);
            (void)cat_put_schema(c, i, &s);
        }
    }
    return true;
}

static bool cat_relations(catalog *c, cat_rows *w, catalog_schema *s) {
    uint64_t n = w->res->rows;
    sc_tslice list = n > 0 ? SC_TSLICE_ALLOC(catalog_relation, c->arena, (size_t)n) : cat_none();
    if (!cat_fits(c, list, n)) {
        return false;
    }
    for (; w->row < n; w->row++) {
        proto_cell a;
        uint8_t kind = 0;
        catalog_relation r = {0};
        r.columns = cat_none();
        r.constraints = cat_none();
        r.indexes = cat_none();
        r.error = cat_empty(c->arena);
        if (!cat_rows_next(w) || !cat_cell(w, &a) || !cat_u32(cat_cell_arena(w), a, &r.oid) ||
            !cat_cell(w, &a) || !cat_text(c, cat_cell_arena(w), a, &r.name) || !cat_cell(w, &a) ||
            !cat_byte(cat_cell_arena(w), a, false, &kind) || !cat_kind(kind, &r.kind) ||
            !cat_cell(w, &a) || !cat_estimate(cat_cell_arena(w), a, &r.estimate_known, &r.estimate) ||
            !SC_TSLICE_SET(catalog_relation, c->arena, list, (size_t)w->row, &r)) {
            return false;
        }
    }
    s->relations = list;
    return true;
}

static bool cat_functions(catalog *c, cat_rows *w, catalog_schema *s) {
    uint64_t n = w->res->rows;
    sc_tslice list = n > 0 ? SC_TSLICE_ALLOC(catalog_function, c->arena, (size_t)n) : cat_none();
    if (!cat_fits(c, list, n)) {
        return false;
    }
    for (; w->row < n; w->row++) {
        proto_cell a;
        catalog_function f = {0};
        if (!cat_rows_next(w) || !cat_cell(w, &a) || !cat_u32(cat_cell_arena(w), a, &f.oid) ||
            !cat_cell(w, &a) || !cat_text(c, cat_cell_arena(w), a, &f.name) || !cat_cell(w, &a) ||
            !cat_byte(cat_cell_arena(w), a, false, &f.kind) || !cat_cell(w, &a) ||
            !cat_text_or_null(c, cat_cell_arena(w), a, &f.args) || !cat_cell(w, &a) ||
            !cat_text_or_null(c, cat_cell_arena(w), a, &f.result) ||
            !SC_TSLICE_SET(catalog_function, c->arena, list, (size_t)w->row, &f)) {
            return false;
        }
    }
    s->functions = list;
    return true;
}

static bool cat_columns(catalog *c, cat_rows *w, catalog_relation *r) {
    uint64_t n = w->res->rows;
    sc_tslice list = n > 0 ? SC_TSLICE_ALLOC(catalog_column, c->arena, (size_t)n) : cat_none();
    if (!cat_fits(c, list, n)) {
        return false;
    }
    for (; w->row < n; w->row++) {
        proto_cell a;
        uint8_t ident = 0, gen = 0;
        catalog_column col = {0};
        bool ok = cat_rows_next(w) && cat_cell(w, &a) && cat_u32(cat_cell_arena(w), a, &col.num) &&
                  cat_cell(w, &a) && cat_text(c, cat_cell_arena(w), a, &col.name) && cat_cell(w, &a) &&
                  cat_text(c, cat_cell_arena(w), a, &col.type) && cat_cell(w, &a) &&
                  cat_bool(cat_cell_arena(w), a, &col.not_null) && cat_cell(w, &a);
        col.has_default = ok && !a.null;
        ok = ok && cat_text_or_null(c, cat_cell_arena(w), a, &col.default_expr) && cat_cell(w, &a) &&
             cat_byte(cat_cell_arena(w), a, true, &ident) && cat_identity(ident, &col.identity) &&
             cat_cell(w, &a) && cat_byte(cat_cell_arena(w), a, true, &gen) &&
             (gen == 0 || gen == 's' || gen == 'v') &&
             cat_cell(w, &a) && cat_u32(cat_cell_arena(w), a, &col.key_pos);
        col.generated = gen == 's' || gen == 'v';
        col.generated_virtual = gen == 'v';
        if (!ok || !SC_TSLICE_SET(catalog_column, c->arena, list, (size_t)w->row, &col)) {
            return false;
        }
    }
    r->columns = list;
    return true;
}

/* conkey as array_to_string(conkey, ' '): attnums, one space apart. */
static bool cat_attnums(catalog *c, const sc_arena *sa, proto_cell cell, sc_tslice *out) {
    if (cell.null || cell.truncated) {
        return false;
    }
    size_t n = cell.bytes.len > 0 ? 1u : 0u;
    for (size_t i = 0; i < cell.bytes.len; i++) {
        uint8_t ch = 0;
        n += sc_slice_get(sa, cell.bytes, i, &ch) && ch == ' ' ? 1u : 0u;
    }
    sc_tslice list = n > 0 ? SC_TSLICE_ALLOC(uint32_t, c->arena, n) : cat_none();
    if (!cat_fits(c, list, (uint64_t)n)) {
        return false;
    }
    size_t k = 0, start = 0;
    for (size_t i = 0; i <= cell.bytes.len && k < n; i++) {
        uint8_t ch = ' ';
        if (i < cell.bytes.len && !sc_slice_get(sa, cell.bytes, i, &ch)) {
            return false;
        }
        if (ch != ' ') {
            continue;
        }
        proto_cell part = cell;
        uint32_t num = 0;
        part.bytes = sc_slice_sub(sa, cell.bytes, start, i - start);
        if (!cat_u32(sa, part, &num) || !SC_TSLICE_SET(uint32_t, c->arena, list, k, &num)) {
            return false;
        }
        k++;
        start = i + 1u;
    }
    *out = list;
    return true;
}

static bool cat_constraints(catalog *c, cat_rows *w, catalog_relation *r) {
    uint64_t n = w->res->rows;
    sc_tslice list = n > 0 ? SC_TSLICE_ALLOC(catalog_constraint, c->arena, (size_t)n) : cat_none();
    if (!cat_fits(c, list, n)) {
        return false;
    }
    for (; w->row < n; w->row++) {
        proto_cell a;
        catalog_constraint k = {0};
        if (!cat_rows_next(w) || !cat_cell(w, &a) || !cat_text(c, cat_cell_arena(w), a, &k.name) ||
            !cat_cell(w, &a) || !cat_byte(cat_cell_arena(w), a, false, &k.type) || !cat_cell(w, &a) ||
            !cat_text_or_null(c, cat_cell_arena(w), a, &k.def) || !cat_cell(w, &a) ||
            !cat_attnums(c, cat_cell_arena(w), a, &k.columns) || !cat_cell(w, &a) ||
            !cat_text_or_null(c, cat_cell_arena(w), a, &k.target) ||
            !SC_TSLICE_SET(catalog_constraint, c->arena, list, (size_t)w->row, &k)) {
            return false;
        }
    }
    r->constraints = list;
    return true;
}

static bool cat_indexes(catalog *c, cat_rows *w, catalog_relation *r) {
    uint64_t n = w->res->rows;
    sc_tslice list = n > 0 ? SC_TSLICE_ALLOC(catalog_index, c->arena, (size_t)n) : cat_none();
    if (!cat_fits(c, list, n)) {
        return false;
    }
    for (; w->row < n; w->row++) {
        proto_cell a;
        catalog_index x = {0};
        if (!cat_rows_next(w) || !cat_cell(w, &a) || !cat_text(c, cat_cell_arena(w), a, &x.name) ||
            !cat_cell(w, &a) || !cat_bool(cat_cell_arena(w), a, &x.primary) || !cat_cell(w, &a) ||
            !cat_bool(cat_cell_arena(w), a, &x.unique) || !cat_cell(w, &a) ||
            !cat_text_or_null(c, cat_cell_arena(w), a, &x.def) ||
            !SC_TSLICE_SET(catalog_index, c->arena, list, (size_t)w->row, &x)) {
            return false;
        }
    }
    r->indexes = list;
    return true;
}

static bool cat_find_schema(const catalog *c, uint32_t oid, size_t *si, catalog_schema *s) {
    for (size_t i = 0; catalog_schema_at(c, i, s); i++) {
        if (s->oid == oid) {
            *si = i;
            return true;
        }
    }
    return false;
}

bool catalog_relation_at(const catalog *c, const catalog_schema *s, size_t i,
                         catalog_relation *out) {
    return SC_TSLICE_GET(catalog_relation, c->arena, s->relations, i, out);
}

bool catalog_function_at(const catalog *c, const catalog_schema *s, size_t i,
                         catalog_function *out) {
    return SC_TSLICE_GET(catalog_function, c->arena, s->functions, i, out);
}

bool catalog_column_at(const catalog *c, const catalog_relation *r, size_t i, catalog_column *out) {
    return SC_TSLICE_GET(catalog_column, c->arena, r->columns, i, out);
}

bool catalog_constraint_at(const catalog *c, const catalog_relation *r, size_t i,
                           catalog_constraint *out) {
    return SC_TSLICE_GET(catalog_constraint, c->arena, r->constraints, i, out);
}

bool catalog_index_at(const catalog *c, const catalog_relation *r, size_t i, catalog_index *out) {
    return SC_TSLICE_GET(catalog_index, c->arena, r->indexes, i, out);
}

bool catalog_find_relation(const catalog *c, uint32_t schema, uint32_t oid, size_t *si,
                           size_t *ri) {
    catalog_schema s;
    catalog_relation r;
    size_t at = 0;
    if (!cat_find_schema(c, schema, &at, &s)) {
        return false;
    }
    for (size_t i = 0; catalog_relation_at(c, &s, i, &r); i++) {
        if (r.oid == oid) {
            *si = at;
            *ri = i;
            return true;
        }
    }
    return false;
}

/* Why rows that came back were not stored: no room, or not the read's shape. */
static sc_str cat_bad_rows(catalog *c) {
    return c->full ? c->room_error
                   : SC_STR_LIT(c->arena, "The catalog read returned rows of an unexpected shape.");
}

static bool cat_definition(catalog *c, cat_rows *w, sc_str *why) {
    uint64_t n = w->res->rows;
    size_t total = 0;
    cat_rows first = *w;
    for (; w->row < n; w->row++) {
        proto_cell a;
        if (!cat_rows_next(w) || !cat_cell(w, &a) || a.null) {
            return false;
        }
        if (a.truncated) {
            *why = SC_STR_LIT(c->defs, "A line of the definition is longer than the sidebar reads.");
            return false;
        }
        if (a.bytes.len > SIZE_MAX - total - 1u) {
            return false;
        }
        total += a.bytes.len + (w->row > 0 ? 1u : 0u);
    }
    if (n == 0) {
        return false;
    }
    sc_slice room = sc_arena_alloc(c->defs, total <= c->defs->cap ? total : 0u);
    if (room.len != total) {
        *why = SC_STR_LIT(c->defs, "The definition is longer than the editor holds.");
        return false;
    }
    *w = first;
    sc_buf b = sc_buf_new(room);
    bool ok = true;
    for (; ok && w->row < n; w->row++) {
        proto_cell a;
        ok = cat_rows_next(w) && cat_cell(w, &a) && (w->row == 0 || SC_BUF_LIT(c->defs, &b, "\n")) &&
             sc_buf_str(c->defs, &b, cat_cell_arena(w), a.bytes);
    }
    if (ok) {
        c->def = sc_buf_view(c->defs, b);
    }
    return ok;
}

static void cat_part(catalog_load *state, size_t *left, sc_str *error, bool ok, sc_str why) {
    *left = *left > 0 ? *left - 1u : 0u;
    if (!ok) {
        *state = CATALOG_LOAD_FAILED;
        *error = why;
    } else if (*left == 0 && *state == CATALOG_LOAD_PENDING) {
        *state = CATALOG_LOAD_DONE;
    }
}

/* The read's outcome lands on its target: `res` NULL is a failure with
 * `error`. False when nothing was stored. */
static bool cat_land(catalog *c, catalog_read r, const result *res, sc_str error) {
    cat_rows w;
    bool rows = res != NULL && cat_rows_start(&w, res, r.kind);
    c->full = false;
    if (r.kind == CATALOG_READ_TIMEOUT || r.kind == CATALOG_READ_SESSION ||
        r.kind == CATALOG_READ_SCHEMAS) {
        bool ok = r.kind == CATALOG_READ_TIMEOUT ? res != NULL && !res->has_columns
                  : r.kind == CATALOG_READ_SESSION
                      ? rows && w.res->rows == 1 && cat_session_row(c, &w)
                      : rows && cat_schemas(c, &w);
        if (!ok) {
            c->schemas_load = CATALOG_LOAD_FAILED;
            c->error = res != NULL ? cat_bad_rows(c) : error;
        } else if (r.kind == CATALOG_READ_SCHEMAS && c->schemas_load == CATALOG_LOAD_PENDING) {
            c->schemas_load = CATALOG_LOAD_DONE;
        }
        return ok;
    }
    if (r.kind == CATALOG_READ_DEFINITION) {
        if (r.oid != c->def_oid || c->def_load != CATALOG_LOAD_PENDING) {
            return false;
        }
        sc_str why = res != NULL ? cat_empty(c->defs) : error;
        bool ok = rows && cat_definition(c, &w, &why);
        c->def_load = ok ? CATALOG_LOAD_DONE : CATALOG_LOAD_FAILED;
        if (!ok) {
            c->def = why.len > 0
                         ? why
                         : SC_STR_LIT(c->defs, "The catalog read returned rows of an unexpected shape.");
        }
        return ok;
    }
    size_t si = 0, ri = 0;
    catalog_schema s;
    if (r.kind == CATALOG_READ_RELATIONS || r.kind == CATALOG_READ_FUNCTIONS) {
        if (!cat_find_schema(c, r.oid, &si, &s) || s.load_left == 0) {
            return false;
        }
        bool ok = rows && (r.kind == CATALOG_READ_RELATIONS ? cat_relations(c, &w, &s)
                                                            : cat_functions(c, &w, &s));
        sc_str why = ok ? error : res != NULL ? cat_bad_rows(c) : error;
        cat_part(&s.load, &s.load_left, &s.error, ok, why);
        (void)cat_put_schema(c, si, &s);
        return ok;
    }
    catalog_relation rel;
    if (!catalog_find_relation(c, r.schema, r.oid, &si, &ri) || !catalog_schema_at(c, si, &s) ||
        !catalog_relation_at(c, &s, ri, &rel) || rel.detail_left == 0) {
        return false;
    }
    bool ok = false;
    switch (r.kind) {
    case CATALOG_READ_COLUMNS:
        ok = rows && cat_columns(c, &w, &rel);
        break;
    case CATALOG_READ_CONSTRAINTS:
        ok = rows && cat_constraints(c, &w, &rel);
        break;
    case CATALOG_READ_INDEXES:
        ok = rows && cat_indexes(c, &w, &rel);
        break;
    default:
        return false;
    }
    sc_str why = ok ? error : res != NULL ? cat_bad_rows(c) : error;
    cat_part(&rel.detail, &rel.detail_left, &rel.error, ok, why);
    (void)SC_TSLICE_SET(catalog_relation, c->arena, s.relations, ri, &rel);
    return ok;
}

bool catalog_take(catalog *c, catalog_read r, const result *res) {
    return r.gen == c->gen && cat_land(c, r, res, cat_empty(c->arena));
}

void catalog_fail(catalog *c, catalog_read r, const sc_arena *ma, sc_str message) {
    sc_str copy;
    sc_arena *a = r.kind == CATALOG_READ_DEFINITION ? c->defs : c->arena;
    if (r.gen != c->gen) {
        return;
    }
    if (!cat_copy(a, ma, message, false, &copy) || copy.len == 0) {
        copy = SC_STR_LIT(a, "The catalog read failed.");
    }
    (void)cat_land(c, r, NULL, copy);
}

bool catalog_expand(catalog *c, size_t i) {
    catalog_schema s;
    if (!catalog_schema_at(c, i, &s)) {
        return false;
    }
    s.expanded = true;
    bool ok = true;
    if (s.load == CATALOG_LOAD_NONE || (s.load == CATALOG_LOAD_FAILED && s.load_left == 0)) {
        ok = cat_queue_schema(c, &s);
    }
    return cat_put_schema(c, i, &s) && ok;
}

bool catalog_collapse(catalog *c, size_t i) {
    catalog_schema s;
    if (!catalog_schema_at(c, i, &s)) {
        return false;
    }
    s.expanded = false;
    return cat_put_schema(c, i, &s);
}

bool catalog_want_definition(catalog *c, uint32_t oid) {
    if (!sc_arena_reset(c->defs)) {
        return false;
    }
    c->def_oid = oid;
    c->def_load = CATALOG_LOAD_PENDING;
    if (!cat_push(c, CATALOG_READ_DEFINITION, oid, 0)) {
        c->def_load = CATALOG_LOAD_FAILED;
        c->def = SC_STR_LIT(c->defs, "Too many catalog reads are waiting.");
        return false;
    }
    return true;
}

bool catalog_take_definition(catalog *c, uint32_t *oid, bool *ok, sc_str *text) {
    if (c->def_load != CATALOG_LOAD_DONE && c->def_load != CATALOG_LOAD_FAILED) {
        return false;
    }
    *oid = c->def_oid;
    *ok = c->def_load == CATALOG_LOAD_DONE;
    *text = c->def;
    c->def_load = CATALOG_LOAD_NONE;
    return true;
}

bool catalog_definition_text(catalog *c, sc_arena *a, uint32_t *oid, const sc_arena **ta, sc_str *text) {
    bool ok = false, cut = false;
    sc_str got;
    if (!catalog_take_definition(c, oid, &ok, &got)) {
        return false;
    }
    *ta = ok ? c->defs : a;
    if (ok) {
        *text = got;
        return true;
    }
    sc_buf b = sc_buf_new(sc_arena_alloc(a, CATALOG_NOTE_MAX + 64u));
    bool fits = SC_BUF_LIT(a, &b, "-- The function's definition could not be read: ");
    for (size_t i = 0; fits && !cut && i < got.len;) {
        sc_utf8_char ch = sc_utf8_next(c->defs, got, i);
        size_t step = ch.len > 0 ? ch.len : 1u;
        cut = step > CATALOG_NOTE_MAX - i;
        fits = cut ? SC_BUF_LIT(a, &b, "\xE2\x80\xA6")
               : cat_line_char(ch) ? sc_buf_str(a, &b, c->defs, sc_slice_sub(c->defs, got, i, step))
                                   : SC_BUF_LIT(a, &b, " ");
        i += step;
    }
    *text = fits ? sc_buf_view(a, b) : cat_empty(a);
    return true;
}

bool catalog_want_detail(catalog *c, uint32_t schema, uint32_t oid) {
    size_t si = 0, ri = 0;
    catalog_schema s;
    catalog_relation r;
    if (!catalog_find_relation(c, schema, oid, &si, &ri) || !catalog_schema_at(c, si, &s) ||
        !catalog_relation_at(c, &s, ri, &r)) {
        return false;
    }
    if (r.detail == CATALOG_LOAD_PENDING || r.detail == CATALOG_LOAD_DONE ||
        r.detail_left > 0) {
        return true;
    }
    bool ok = cat_room(c, 3) && cat_push(c, CATALOG_READ_COLUMNS, oid, schema) &&
              cat_push(c, CATALOG_READ_CONSTRAINTS, oid, schema) &&
              cat_push(c, CATALOG_READ_INDEXES, oid, schema);
    r.detail = ok ? CATALOG_LOAD_PENDING : CATALOG_LOAD_FAILED;
    r.detail_left = ok ? 3u : 0u;
    if (!ok) {
        r.error = SC_STR_LIT(c->arena, "Too many catalog reads are waiting.");
    }
    return SC_TSLICE_SET(catalog_relation, c->arena, s.relations, ri, &r) && ok;
}

bool catalog_quote_ident(const sc_arena *sa, sc_str name, sc_arena *a, sc_str *out) {
    size_t quotes = 0;
    for (size_t i = 0; i < name.len; i++) {
        uint8_t ch = 0;
        if (!sc_slice_get(sa, name, i, &ch) || ch == 0) {
            return false;
        }
        quotes += ch == '"' ? 1u : 0u;
    }
    if (name.len > (SIZE_MAX - 2u) / 2u) {
        return false;
    }
    sc_buf b = sc_buf_new(sc_arena_alloc(a, name.len + quotes + 2u));
    bool ok = SC_BUF_LIT(a, &b, "\"");
    for (size_t i = 0; ok && i < name.len; i++) {
        uint8_t ch = 0;
        ok = sc_slice_get(sa, name, i, &ch) && sc_buf_lit(a, &b, &ch, 1) &&
             (ch != '"' || SC_BUF_LIT(a, &b, "\""));
    }
    if (!ok || !SC_BUF_LIT(a, &b, "\"")) {
        return false;
    }
    *out = sc_buf_view(a, b);
    return true;
}

bool catalog_select_all(const catalog *c, uint32_t schema, uint32_t oid, sc_arena *a, sc_str *out) {
    size_t si = 0, ri = 0;
    catalog_schema s;
    catalog_relation r;
    sc_str qs, qt;
    if (!catalog_find_relation(c, schema, oid, &si, &ri) || !catalog_schema_at(c, si, &s) ||
        !catalog_relation_at(c, &s, ri, &r) || !cat_one_line(c->arena, s.name) ||
        !cat_one_line(c->arena, r.name) || !catalog_quote_ident(c->arena, s.name, a, &qs) ||
        !catalog_quote_ident(c->arena, r.name, a, &qt) || qs.len > SIZE_MAX - 32u - qt.len) {
        return false;
    }
    sc_buf q = sc_buf_new(sc_arena_alloc(a, qs.len + qt.len + 32u));
    if (!SC_BUF_LIT(a, &q, "SELECT * FROM ") || !sc_buf_str(a, &q, a, qs) || !SC_BUF_LIT(a, &q, ".") ||
        !sc_buf_str(a, &q, a, qt) || !SC_BUF_LIT(a, &q, ";")) {
        return false;
    }
    *out = sc_buf_view(a, q);
    return true;
}

catalog_show catalog_shown(const catalog *c, catalog_link query, catalog_link meta) {
    if (query == CATALOG_LINK_DOWN || query == CATALOG_LINK_FAILED) {
        return CATALOG_SHOW_DISCONNECTED;
    }
    if (query == CATALOG_LINK_CONNECTING) {
        return CATALOG_SHOW_CONNECTING;
    }
    if (meta == CATALOG_LINK_FAILED) {
        return CATALOG_SHOW_ERROR;
    }
    return meta != CATALOG_LINK_UP                       ? CATALOG_SHOW_LOADING
           : c->schemas_load == CATALOG_LOAD_FAILED ? CATALOG_SHOW_ERROR
           : c->schemas_load == CATALOG_LOAD_DONE   ? CATALOG_SHOW_READY
                                                    : CATALOG_SHOW_LOADING;
}
