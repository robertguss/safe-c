#ifndef EXPLORER_ENGINE_POLICY_H
#define EXPLORER_ENGINE_POLICY_H

#include "../../adapters/sc_db.h"
#include "sqltok.h"

/* Session-facing engine facts so UI/nav/page/meta stop testing engines. */
typedef struct {
    sc_db_engine engine;
    uint8_t dialect; /* SQLTOK_* == SC_DB_* */
    bool file_target;
    bool has_pg_timeouts;
    bool has_request_control; /* link-owned cancel signal for embedded engines */
    bool nav_binds_names;
    bool catalog_gen_selection; /* tree selection follows catalog gen */
    bool unordered_needs_key;
    bool has_functions_browser;
    bool has_relation_definition; /* Structure → Definition for local tables/views */
    bool duckdb_files;
    bool partial_constraints; /* SQLite: label Constraints as incomplete */
} engine_policy;

static inline engine_policy engine_policy_of(sc_db_engine engine) {
    engine_policy p = {0};
    p.engine = engine;
    p.dialect = (uint8_t)engine;
    p.file_target = engine != SC_DB_POSTGRESQL;
    p.has_pg_timeouts = engine == SC_DB_POSTGRESQL;
    p.has_request_control = engine != SC_DB_POSTGRESQL;
    p.nav_binds_names = engine != SC_DB_POSTGRESQL;
    p.catalog_gen_selection = engine != SC_DB_POSTGRESQL;
    p.unordered_needs_key = engine != SC_DB_POSTGRESQL;
    p.has_functions_browser = engine == SC_DB_POSTGRESQL;
    p.has_relation_definition = engine != SC_DB_POSTGRESQL;
    p.duckdb_files = engine == SC_DB_DUCKDB;
    p.partial_constraints = engine == SC_DB_SQLITE;
    return p;
}

_Static_assert((uint8_t)SC_DB_POSTGRESQL == SQLTOK_POSTGRESQL, "dialect ids match");
_Static_assert((uint8_t)SC_DB_DUCKDB == SQLTOK_DUCKDB, "dialect ids match");
_Static_assert((uint8_t)SC_DB_SQLITE == SQLTOK_SQLITE, "dialect ids match");

#endif
