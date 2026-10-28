#include "catalog_provider.h"

#define CAT_SQL_BYTES 1024u
#define CAT_NAME_MAX (SIZE_MAX / 8u)

void cat_lit(cat_sql_text *t, const uint8_t *lit, size_t n) {
    t->ok = t->ok && sc_buf_lit(t->a, &t->b, lit, n);
}

static void cat_oid(cat_sql_text *t, uint32_t oid) {
    t->ok = t->ok && sc_buf_u64(t->a, &t->b, oid);
}

static void cat_ident(cat_sql_text *t, const catalog *c, sc_str name) {
    sc_str q;
    t->ok = t->ok && catalog_quote_ident(c->arena, name, t->a, &q) && sc_buf_str(t->a, &t->b, t->a, q);
}

bool catalog_sql(const catalog *c, catalog_read r, sc_arena *a, sc_str *out) {
    catalog_schema s = {0};
    catalog_relation rel = {0};
    size_t room = CAT_SQL_BYTES;
    if (c->engine != SC_DB_POSTGRESQL) {
        if (r.kind == CATALOG_READ_RELATIONS) {
            bool found = false;
            for (size_t i = 0; !found && catalog_schema_at(c, i, &s); i++) found = s.oid == r.oid;
            if (!found) return false;
        } else if (r.kind == CATALOG_READ_COLUMNS || r.kind == CATALOG_READ_CONSTRAINTS ||
                   r.kind == CATALOG_READ_INDEXES || r.kind == CATALOG_READ_DEFINITION ||
                   r.kind == CATALOG_READ_COUNT) {
            if (!catalog_relation_of(c, r.schema, r.oid, &s, &rel)) return false;
        }
        if (s.name.len > (SIZE_MAX - 8192u) / 16u || rel.name.len > (SIZE_MAX - 8192u) / 16u)
            return false;
        room = 8u * s.name.len + 8u * rel.name.len + 8192u;
        if (r.kind != CATALOG_READ_COUNT) {
            cat_sql_text native = {a, sc_buf_new(sc_arena_alloc(a, room)), true};
            bool written = c->engine == SC_DB_DUCKDB
                ? catalog_duckdb_sql(&native, r.kind, c, &s, &rel)
                : catalog_sqlite_sql(&native, r.kind, c, &rel);
            if (!written || !native.ok) return false;
            *out = sc_buf_view(a, native.b);
            return true;
        }
    }
    if (r.kind == CATALOG_READ_COUNT) {
        if (!catalog_relation_of(c, r.schema, r.oid, &s, &rel) || s.name.len > CAT_NAME_MAX ||
            rel.name.len > CAT_NAME_MAX) {
            return false;
        }
        room = 2u * s.name.len + 2u * rel.name.len + 64u;
    }
    cat_sql_text t = {a, sc_buf_new(sc_arena_alloc(a, room)), true};
    switch (r.kind) {
    case CATALOG_READ_TIMEOUT:
        CAT_LIT(&t, "SET statement_timeout = '10s'");
        break;
    case CATALOG_READ_SESSION:
        CAT_LIT(&t, "SELECT pg_catalog.current_setting('server_version_num'), "
                    "pg_catalog.current_setting('server_version'), "
                    "pg_catalog.current_setting('standard_conforming_strings'), "
                    "pg_catalog.current_database(), current_user, "
                    "COALESCE(pg_catalog.host(pg_catalog.inet_server_addr()), 'localhost')");
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
    case CATALOG_READ_COUNT_TIMEOUT:
        CAT_LIT(&t, "SET statement_timeout = '2s'");
        break;
    case CATALOG_READ_COUNT:
        CAT_LIT(&t, "SELECT count(*) FROM ");
        cat_ident(&t, c, s.name);
        CAT_LIT(&t, ".");
        cat_ident(&t, c, rel.name);
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

static bool cat_quote(const sc_arena *sa, sc_str name, sc_arena *a, uint8_t quote, sc_str *out) {
    size_t quotes = 0;
    for (size_t i = 0; i < name.len; i++) {
        uint8_t ch = 0;
        if (!sc_slice_get(sa, name, i, &ch) || ch == 0) {
            return false;
        }
        quotes += ch == quote ? 1u : 0u;
    }
    if (name.len > (SIZE_MAX - 2u) / 2u) {
        return false;
    }
    sc_buf b = sc_buf_new(sc_arena_alloc(a, name.len + quotes + 2u));
    bool ok = sc_buf_lit(a, &b, &quote, 1);
    for (size_t i = 0; ok && i < name.len; i++) {
        uint8_t ch = 0;
        ok = sc_slice_get(sa, name, i, &ch) && sc_buf_lit(a, &b, &ch, 1) &&
             (ch != quote || sc_buf_lit(a, &b, &quote, 1));
    }
    if (!ok || !sc_buf_lit(a, &b, &quote, 1)) {
        return false;
    }
    *out = sc_buf_view(a, b);
    return true;
}

bool catalog_quote_ident(const sc_arena *sa, sc_str name, sc_arena *a, sc_str *out) {
    return cat_quote(sa, name, a, '"', out);
}

void cat_string(cat_sql_text *t, const catalog *c, sc_str name) {
    sc_str quoted;
    t->ok = t->ok && cat_quote(c->arena, name, t->a, '\'', &quoted) &&
            sc_buf_str(t->a, &t->b, t->a, quoted);
}
