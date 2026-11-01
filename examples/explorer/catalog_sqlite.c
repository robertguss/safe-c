#include "catalog_provider.h"

static void sq_catalog_xinfo(cat_sql_text *t, const catalog *c, const catalog_relation *r) {
    CAT_LIT(t, "pragma_table_xinfo(");
    cat_string(t, c, r->name);
    CAT_LIT(t, ", 'main')");
}

bool catalog_sqlite_sql(cat_sql_text *t, catalog_read_kind kind, const catalog *c,
                        const catalog_relation *r) {
    switch (kind) {
    case CATALOG_READ_SESSION:
        CAT_LIT(t, "SELECT sqlite_version()");
        break;
    case CATALOG_READ_SCHEMAS:
        CAT_LIT(t, "SELECT 'main'");
        break;
    case CATALOG_READ_RELATIONS:
        CAT_LIT(t, "SELECT name, CASE type WHEN 'table' THEN 'r' ELSE 'v' END, NULL "
                   "FROM main.sqlite_schema WHERE type IN ('table','view') AND "
                   "substr(name,1,7) <> 'sqlite_' ORDER BY name");
        break;
    case CATALOG_READ_COLUMNS:
        CAT_LIT(t, "WITH cols AS (SELECT * FROM ");
        sq_catalog_xinfo(t, c, r);
        CAT_LIT(t, "), ix AS (SELECT * FROM pragma_index_list(");
        cat_string(t, c, r->name);
        CAT_LIT(t, ", 'main')) SELECT cid + 1, name, CASE WHEN type = '' THEN 'unknown/mixed' ELSE type END, "
                   "\"notnull\" OR (pk = 1 AND upper(type) = 'INTEGER' AND "
                   "(SELECT count(*) FROM cols WHERE pk > 0) = 1 AND "
                   "NOT EXISTS (SELECT 1 FROM ix WHERE origin = 'pk')), dflt_value, '', "
                   "CASE hidden WHEN 2 THEN 'v' WHEN 3 THEN 's' ELSE '' END, pk, hidden = 1 "
                   "FROM cols ORDER BY cid");
        break;
    case CATALOG_READ_CONSTRAINTS:
        CAT_LIT(t, "WITH cols AS (SELECT * FROM ");
        sq_catalog_xinfo(t, c, r);
        CAT_LIT(t, "), fk AS (SELECT * FROM pragma_foreign_key_list(");
        cat_string(t, c, r->name);
        CAT_LIT(t, ", 'main')), ix AS (SELECT * FROM pragma_index_list(");
        cat_string(t, c, r->name);
        CAT_LIT(t, ", 'main')) SELECT 'Foreign key ' || id, 'f', "
                   "'FOREIGN KEY (' || group_concat('\"' || replace(\"from\",'\"','\"\"') || '\"', ', ' ORDER BY seq) || "
                   "') REFERENCES \"' || replace(\"table\",'\"','\"\"') || '\" (' || "
                   "group_concat(CASE WHEN \"to\" IS NULL THEN '<primary key>' ELSE "
                   "'\"' || replace(\"to\",'\"','\"\"') || '\"' END, ', ' ORDER BY seq) || ')', "
                   "group_concat((SELECT cid + 1 FROM cols WHERE name = fk.\"from\" COLLATE NOCASE), ' ' ORDER BY seq), "
                   "\"table\" FROM fk GROUP BY id UNION ALL SELECT ix.name, 'u', NULL, "
                   "COALESCE((SELECT group_concat(cid + 1, ' ' ORDER BY seqno) "
                   "FROM pragma_index_xinfo(ix.name, 'main') WHERE key = 1 AND cid >= 0), ''), NULL "
                   "FROM ix WHERE origin = 'u' ORDER BY 1");
        break;
    case CATALOG_READ_INDEXES:
        CAT_LIT(t, "SELECT i.name, i.origin = 'pk', i.\"unique\", "
                   "COALESCE(s.sql, 'Automatic ' || CASE i.origin WHEN 'pk' THEN 'primary-key' ELSE 'unique' END || "
                   "' index (' || COALESCE((SELECT group_concat("
                   "COALESCE('\"' || replace(name,'\"','\"\"') || '\"', '<expression>'), ', ' ORDER BY seqno) "
                   "FROM pragma_index_xinfo(i.name, 'main') WHERE key = 1), '') || ')') "
                   "FROM pragma_index_list(");
        cat_string(t, c, r->name);
        CAT_LIT(t, ", 'main') i LEFT JOIN main.sqlite_schema s ON s.type = 'index' AND s.name = i.name "
                   "ORDER BY i.origin = 'pk' DESC, i.\"unique\" DESC, i.name");
        break;
    case CATALOG_READ_DEFINITION:
        CAT_LIT(t, "WITH RECURSIVE definition(sql) AS (SELECT sql FROM main.sqlite_schema "
                   "WHERE type IN ('table','view') AND name = ");
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
