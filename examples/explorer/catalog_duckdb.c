#include "catalog_provider.h"

static void dd_catalog_where(cat_sql_text *t, const catalog *c, const catalog_schema *s,
                              const catalog_relation *r, bool relation) {
    CAT_LIT(t, " WHERE database_name = current_database() AND schema_name = ");
    cat_string(t, c, s->name);
    if (relation) {
        CAT_LIT(t, " AND table_name = ");
        cat_string(t, c, r->name);
    }
}

bool catalog_duckdb_sql(cat_sql_text *t, catalog_read_kind kind, const catalog *c,
                        const catalog_schema *s, const catalog_relation *r) {
    switch (kind) {
    case CATALOG_READ_SESSION:
        CAT_LIT(t, "SELECT version()");
        break;
    case CATALOG_READ_SCHEMAS:
        CAT_LIT(t, "SELECT schema_name FROM duckdb_schemas() "
                   "WHERE database_name = current_database() ORDER BY schema_name");
        break;
    case CATALOG_READ_RELATIONS:
        CAT_LIT(t, "SELECT table_name, 'r', estimated_size FROM duckdb_tables()");
        dd_catalog_where(t, c, s, r, false);
        CAT_LIT(t, " AND NOT internal AND NOT temporary UNION ALL SELECT view_name, 'v', NULL "
                   "FROM duckdb_views() WHERE database_name = current_database() AND schema_name = ");
        cat_string(t, c, s->name);
        CAT_LIT(t, " AND NOT internal AND NOT temporary ORDER BY 1");
        break;
    case CATALOG_READ_COLUMNS:
        CAT_LIT(t, "WITH cols AS (SELECT * FROM duckdb_columns()");
        dd_catalog_where(t, c, s, r, true);
        CAT_LIT(t, "), keys AS (SELECT * FROM duckdb_constraints()");
        dd_catalog_where(t, c, s, r, true);
        CAT_LIT(t, " AND constraint_type = 'PRIMARY KEY') SELECT column_index, column_name, data_type, "
                   "NOT is_nullable, COALESCE(generation_expression, column_default), '', "
                   "CASE WHEN is_generated THEN 'v' ELSE '' END, "
                   "COALESCE((SELECT list_position(constraint_column_names, cols.column_name) FROM keys), 0) "
                   "FROM cols ORDER BY column_index");
        break;
    case CATALOG_READ_CONSTRAINTS:
        CAT_LIT(t, "SELECT constraint_name, CASE constraint_type WHEN 'PRIMARY KEY' THEN 'p' "
                   "WHEN 'UNIQUE' THEN 'u' WHEN 'FOREIGN KEY' THEN 'f' WHEN 'CHECK' THEN 'c' END, "
                   "constraint_text, COALESCE(array_to_string("
                   "list_transform(constraint_column_indexes, lambda x: x + 1), ' '), ''), referenced_table "
                   "FROM duckdb_constraints()");
        dd_catalog_where(t, c, s, r, true);
        CAT_LIT(t, " AND constraint_type IN ('PRIMARY KEY','UNIQUE','FOREIGN KEY','CHECK') ORDER BY 1");
        break;
    case CATALOG_READ_INDEXES:
        CAT_LIT(t, "SELECT index_name, is_primary, is_unique, sql FROM duckdb_indexes()");
        dd_catalog_where(t, c, s, r, true);
        CAT_LIT(t, " ORDER BY is_primary DESC, is_unique DESC, index_name");
        break;
    case CATALOG_READ_DEFINITION:
        CAT_LIT(t, "WITH RECURSIVE definition(sql) AS (SELECT sql FROM duckdb_tables()");
        dd_catalog_where(t, c, s, r, true);
        CAT_LIT(t, " UNION ALL SELECT sql FROM duckdb_views() WHERE database_name = current_database() "
                   "AND schema_name = ");
        cat_string(t, c, s->name);
        CAT_LIT(t, " AND view_name = ");
        cat_string(t, c, r->name);
        CAT_LIT(t, "), chunks(n) AS (SELECT 1 UNION ALL SELECT n + 1000 FROM chunks, definition "
                   "WHERE n + 1000 <= length(sql)) SELECT substr(sql, n, 1000) "
                   "FROM definition, chunks ORDER BY n");
        break;
    default:
        return false;
    }
    return t->ok;
}
