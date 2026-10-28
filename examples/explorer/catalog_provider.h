#ifndef EXPLORER_CATALOG_PROVIDER_H
#define EXPLORER_CATALOG_PROVIDER_H

#include "catalog.h"

typedef struct {
    sc_arena *a;
    sc_buf b;
    bool ok;
} cat_sql_text;

/* Per-engine catalog shape and decode policy (KTD6). SQL writers live in
 * catalog_*.c; decode in catalog.c uses these capabilities instead of
 * scattering `engine ==` tests. */
typedef struct {
    uint8_t session_cols;
    uint8_t schema_cols;
    uint8_t relation_cols;
    uint8_t column_cols;
    bool has_functions;
    bool has_timeouts;
    bool pg_bool;                 /* t/f bytes; otherwise 0/1/true/false text */
    bool synthetic_oids;          /* assign next_oid instead of reading oid cells */
    bool definition_joins_newline; /* PG returns one line per row */
    bool reject_truncated_text;   /* local engines: truncated cells are errors */
    bool null_estimate_ok;        /* local relations may send NULL estimates */
    bool has_hidden_column;       /* SQLite pragma_table_xinfo hidden flag */
} catalog_caps;

const catalog_caps *catalog_caps_of(sc_db_engine engine);
uint64_t catalog_cols_for(const catalog *c, catalog_read_kind k);

void cat_lit(cat_sql_text *t, const uint8_t *lit, size_t n);
void cat_string(cat_sql_text *t, const catalog *c, sc_str name);
#define CAT_LIT(t, lit) cat_lit((t), (const uint8_t *)("" lit), sizeof("" lit) - 1)

bool catalog_duckdb_sql(cat_sql_text *t, catalog_read_kind kind, const catalog *c,
                        const catalog_schema *s, const catalog_relation *r);
bool catalog_sqlite_sql(cat_sql_text *t, catalog_read_kind kind, const catalog *c,
                        const catalog_relation *r);

#endif
