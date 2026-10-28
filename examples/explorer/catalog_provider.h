#ifndef EXPLORER_CATALOG_PROVIDER_H
#define EXPLORER_CATALOG_PROVIDER_H

#include "catalog.h"

typedef struct {
    sc_arena *a;
    sc_buf b;
    bool ok;
} cat_sql_text;

void cat_lit(cat_sql_text *t, const uint8_t *lit, size_t n);
void cat_string(cat_sql_text *t, const catalog *c, sc_str name);
#define CAT_LIT(t, lit) cat_lit((t), (const uint8_t *)("" lit), sizeof("" lit) - 1)

bool catalog_duckdb_sql(cat_sql_text *t, catalog_read_kind kind, const catalog *c,
                        const catalog_schema *s, const catalog_relation *r);
bool catalog_sqlite_sql(cat_sql_text *t, catalog_read_kind kind, const catalog *c,
                        const catalog_relation *r);

#endif
