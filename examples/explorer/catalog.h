#ifndef EXPLORER_CATALOG_H
#define EXPLORER_CATALOG_H

/* The catalog model (plan U18, KTD21, KTD22): what the metadata connection
 * has read about the server's schemas, relations, functions, columns,
 * constraints and indexes, as records in one catalog arena. Core code: it
 * includes only the stdlib and result.h.
 *
 * Reads are lazy and queued: the link's 10 s statement_timeout, the session
 * and the schema list when the metadata link connects (catalog_restart), a schema's relations and
 * functions when it is first expanded, and a relation's columns,
 * constraints and indexes when a view first needs them. A collapsed schema
 * (catalog_collapse) keeps its records.
 *
 * A function's definition (pg_get_functiondef, plan U19) is read when the
 * sidebar opens it (catalog_want_definition), one row per line so no line
 * is cut at the worker's cell cap; a line longer than the cap fails the
 * read, and so does a definition longer than the definition arena, which is
 * sized to the editor's input box. Asking again replaces the request, and a
 * read for an older one is dropped. Each request resets the definition
 * arena, so opening functions does not grow the catalog arena.
 * catalog_take_definition reports, once, the function's oid, whether it
 * loaded, and its text or why not, live in the definition arena until the
 * next request. catalog_definition_text takes it as the text a new tab
 * opens with (`*text`, live in `*ta`): the definition, or a comment line in
 * `a` that says why it could not be read, the reason's control characters
 * and invalid bytes as spaces and cut with an ellipsis past
 * CATALOG_NOTE_MAX bytes. The glue pops each read, runs its SQL as its own
 * RUN on the metadata link (one statement per RUN, KTD12), and hands the
 * finished result back to catalog_take, or its error to catalog_fail. The
 * SQL filters by oid literals; only a count names its relation, quoted.
 *
 * Records are values in sc_tslices, addressed by row index; a relation is
 * found by oid with a scan of its schema's records (no hash map, KTD22).
 * Strings are copies in the catalog arena: a result's arenas can go back to
 * the pool as soon as it is taken. A restart (a reload or a new metadata
 * link) resets the arena and keeps which schemas were expanded, by name. */

#include "result.h"

#define CATALOG_QUEUE 256u     /* reads waiting */
#define CATALOG_NOTE_MAX 448u

typedef enum {
    CATALOG_READ_TIMEOUT,     /* SET statement_timeout = '10s' (KTD21): no rows */
    CATALOG_READ_SESSION,     /* server version, standard_conforming_strings, database, user, host */
    CATALOG_READ_SCHEMAS,
    CATALOG_READ_RELATIONS,   /* of schema `oid` */
    CATALOG_READ_FUNCTIONS,   /* of schema `oid` */
    CATALOG_READ_COLUMNS,     /* of relation `oid` in schema `schema` */
    CATALOG_READ_CONSTRAINTS,
    CATALOG_READ_INDEXES,
    CATALOG_READ_DEFINITION,
    CATALOG_READ_COUNT_TIMEOUT, /* SET statement_timeout = '2s', before a count: no rows */
    CATALOG_READ_COUNT          /* count(*) of relation `oid` in schema `schema` */
} catalog_read_kind;

typedef struct {
    catalog_read_kind kind;
    uint32_t oid;    /* the schema or relation read */
    uint32_t schema; /* a relation's schema */
    uint64_t gen;    /* the restart it belongs to: a read from before one is dropped */
} catalog_read;

typedef enum {
    CATALOG_LOAD_NONE,    /* not asked for */
    CATALOG_LOAD_PENDING, /* queued or running */
    CATALOG_LOAD_DONE,
    CATALOG_LOAD_FAILED   /* `error` says why */
} catalog_load;

typedef enum {
    CATALOG_REL_TABLE,       /* r */
    CATALOG_REL_PARTITIONED, /* p: its partitions are not listed */
    CATALOG_REL_VIEW,        /* v */
    CATALOG_REL_MATVIEW,     /* m */
    CATALOG_REL_FOREIGN      /* f */
} catalog_rel_kind;

typedef enum { CATALOG_IDENTITY_NONE, CATALOG_IDENTITY_ALWAYS, CATALOG_IDENTITY_BY_DEFAULT } catalog_identity;

typedef struct {
    bool loaded;
    uint32_t version_num; /* server_version_num, such as 150019 */
    sc_str version;       /* server_version */
    bool standard_strings; /* standard_conforming_strings is on */
    sc_str database, user;
    sc_str host; /* inet_server_addr(), or localhost over a Unix socket */
} catalog_session;

typedef struct {
    uint32_t oid;
    sc_str name;
    catalog_rel_kind kind;
    bool estimate_known; /* reltuples was not -1 (never vacuumed or analysed) */
    uint64_t estimate;   /* rows, from reltuples */
    catalog_load detail; /* columns, constraints and indexes */
    size_t detail_left;  /* of the three reads, those not yet back */
    bool columns_loaded; /* the columns read landed, with the primary key's positions */
    catalog_load count_load; /* the last count(*) asked for (catalog_want_count) */
    uint64_t count;
    sc_tslice columns;   /* catalog_column */
    sc_tslice constraints; /* catalog_constraint */
    sc_tslice indexes;   /* catalog_index */
    sc_str error;
} catalog_relation;

typedef struct {
    uint32_t oid;
    sc_str name;
    uint8_t kind;   /* prokind: f, p or w; aggregates are not listed */
    sc_str args;    /* pg_get_function_identity_arguments */
    sc_str result;  /* pg_get_function_result; empty for a procedure */
} catalog_function;

typedef struct {
    uint32_t num;      /* attnum */
    sc_str name;
    sc_str type;       /* format_type */
    bool not_null;
    bool has_default;
    sc_str default_expr; /* the default, or a stored generated column's expression */
    catalog_identity identity;
    bool generated;    /* attgenerated 's' (stored) or 'v' (virtual, PG 18) */
    bool generated_virtual;
    uint32_t key_pos;  /* 1-based place in the primary key; 0 for none */
} catalog_column;

typedef struct {
    sc_str name;
    uint8_t type; /* contype: p, u, f, c, x or t; NOT NULL ('n', PG 18) is not listed */
    sc_str def;   /* pg_get_constraintdef */
    sc_tslice columns; /* uint32_t conkey attnums, as catalog_column.num; none for a trigger */
    sc_str target;     /* a foreign key's referenced table (confrelid::regclass); empty otherwise */
} catalog_constraint;

typedef struct {
    sc_str name;
    bool primary, unique;
    sc_str def; /* pg_get_indexdef */
} catalog_index;

typedef struct {
    bool expanded;
    uint32_t oid;
    sc_str name;
    catalog_load load; /* relations and functions */
    size_t load_left;
    sc_tslice relations; /* catalog_relation, in name order */
    sc_tslice functions; /* catalog_function, in name order */
    sc_str error;
} catalog_schema;

typedef struct {
    sc_arena *arena;  /* the records, the queue and every string */
    sc_arena *keep;   /* the expanded schemas' names across a restart */
    sc_arena *defs;   /* the last function definition asked for */
    sc_tslice kept;   /* sc_str in `keep` */
    size_t kept_count;
    uint64_t gen;
    catalog_session session;
    catalog_load schemas_load; /* the timeout, the session and the schema list */
    sc_str error;
    bool full;         /* the read being stored ran out of room */
    sc_str room_error; /* what a failed load says then, written at each restart */
    sc_tslice schemas; /* catalog_schema, in name order */
    catalog_load def_load;
    uint32_t def_oid;
    sc_str def;
    sc_tslice queue;   /* catalog_read, a ring */
    size_t head, queued;
    uint32_t count_oid; /* the relation whose count was asked for last */
} catalog;

/* An empty catalog over `a` (the records), `keep` and `defs` (a function's
 * definition): nothing loaded and nothing queued until catalog_restart. */
bool catalog_init(catalog *c, sc_arena *a, sc_arena *keep, sc_arena *defs);

/* A reload, or a new metadata link: forgets every record and queued read,
 * keeps the names of the expanded schemas (those kept before, while the last
 * restart's schema list has not landed), and queues the timeout, the session
 * and the schema list. Schemas that come back under a kept name are expanded
 * again. False when an arena has no room. */
bool catalog_restart(catalog *c);

/* Pops the next read. A count's 2 s timeout or count(*) is skipped when its
 * relation is not the one counted last (its count_load goes back to NONE)
 * or its count has already failed, so a table left behind does not hold the
 * link; the 10 s timeout after it always runs. False when none waits. */
bool catalog_next(catalog *c, catalog_read *out);

/* Writes read `r`'s SQL into `a` (catalog_sql.c): a count's names the
 * relation, quoted (catalog_quote_ident). False when the records have no
 * such relation or there is no room. */
bool catalog_sql(const catalog *c, catalog_read r, sc_arena *a, sc_str *out);

/* Stores the rows of `res`, the finished result of read `r`. A result whose
 * columns are not the ones the read selects, or a cell that does not parse,
 * stores nothing and marks the read's target failed; so does a catalog arena
 * with no room for the records, with a message that says to reload. A read from before a
 * restart, or whose target is gone, is dropped. False when nothing was
 * stored. */
bool catalog_take(catalog *c, catalog_read r, const result *res);

/* Read `r` ended without rows: its target is marked failed with `message`
 * (live in `ma`). */
void catalog_fail(catalog *c, catalog_read r, const sc_arena *ma, sc_str message);

/* Expands schema `i` and queues its relations and functions unless they are
 * loaded or loading; a failed load is asked for again. */
bool catalog_expand(catalog *c, size_t i);

bool catalog_collapse(catalog *c, size_t i);

bool catalog_want_definition(catalog *c, uint32_t oid);
bool catalog_take_definition(catalog *c, uint32_t *oid, bool *ok, sc_str *text);
bool catalog_definition_text(catalog *c, sc_arena *a, uint32_t *oid, const sc_arena **ta, sc_str *text);

/* Queues the columns, constraints and indexes of relation `oid` in schema
 * `schema` unless they are loaded or loading; a failed load is asked for
 * again. False when the schema's records have no such relation. */
bool catalog_want_detail(catalog *c, uint32_t schema, uint32_t oid);

/* Queues a count(*) of relation `oid` in schema `schema` (plan U20) as
 * three reads, so it holds the metadata link for at most 2 s: a 2 s
 * statement_timeout, the count, and the link's 10 s timeout back. Each call
 * counts again, unless a count is pending, and makes `oid` the one relation
 * counted (catalog_next skips the others). The relation's count_load is
 * DONE with `count`, or FAILED when the count timed out or failed. False
 * when the records have no such relation or the queue has no room. */
bool catalog_want_count(catalog *c, uint32_t schema, uint32_t oid);

/* Lookups by index, copied out. False past the end. */
size_t catalog_schema_count(const catalog *c);
bool catalog_schema_at(const catalog *c, size_t i, catalog_schema *out);
bool catalog_relation_at(const catalog *c, const catalog_schema *s, size_t i,
                         catalog_relation *out);
bool catalog_function_at(const catalog *c, const catalog_schema *s, size_t i,
                         catalog_function *out);
bool catalog_column_at(const catalog *c, const catalog_relation *r, size_t i, catalog_column *out);
bool catalog_constraint_at(const catalog *c, const catalog_relation *r, size_t i,
                           catalog_constraint *out);
bool catalog_index_at(const catalog *c, const catalog_relation *r, size_t i, catalog_index *out);

/* The index `*si` of schema `schema` and `*ri` of its relation `oid`: a scan
 * of the schema list and that one schema's records. catalog_relation_of
 * copies both records out instead. */
bool catalog_find_relation(const catalog *c, uint32_t schema, uint32_t oid, size_t *si,
                           size_t *ri);
bool catalog_relation_of(const catalog *c, uint32_t schema, uint32_t oid, catalog_schema *s,
                         catalog_relation *r);

/* `name` (live in `sa`) as a quoted identifier in `a` (catalog_sql.c):
 * always in double quotes, each `"` doubled. False for a name with a NUL
 * byte or no room. */
bool catalog_quote_ident(const sc_arena *sa, sc_str name, sc_arena *a, sc_str *out);

/* What the sidebar shows (plan U18 step 1), from the query link's state, the
 * metadata link's and what has loaded. While the query link is down or
 * failed the sidebar shows its last tree greyed and "Disconnected", with no
 * Retry: Reconnect rebuilds both links. With the query link up, a failed
 * metadata link or schema list shows its error with a Retry of its own. */
typedef enum { CATALOG_LINK_DOWN, CATALOG_LINK_CONNECTING, CATALOG_LINK_UP, CATALOG_LINK_FAILED } catalog_link;
typedef enum {
    CATALOG_SHOW_CONNECTING,
    CATALOG_SHOW_LOADING,
    CATALOG_SHOW_READY,
    CATALOG_SHOW_ERROR,
    CATALOG_SHOW_DISCONNECTED
} catalog_show;
catalog_show catalog_shown(const catalog *c, catalog_link query, catalog_link meta);

#endif
