#define _POSIX_C_SOURCE 200809L
#include "../examples/explorer/link.h"
#include "../examples/explorer/query_text.h"
#include <sqlite3.h>
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static link_pool pool;
static sc_wake bell;
static conn_link query, meta;
static alignas(8) uint8_t text_mem[128u * 1024u];
static sc_arena text;
static char directory[] = "/tmp/safe-c-sqlite-test-XXXXXX";
static char path[256];
static uint32_t generation = 1;

static sc_str str(const char *s) {
    assert(sc_arena_reset(&text));
    return sc_str_from_lit(&text, (const uint8_t *)s, strlen(s));
}

static void ring(void *arg) { (void)sc_wake_ring(*(sc_wake *)arg); }

#define WAIT(link, condition, ms) do { \
    uint64_t until = sc_clock_ms() + (ms); \
    while (!(condition)) { \
        if (sc_clock_ms() >= until || (link)->broken) { \
            fprintf(stderr, "SQLite wait failed at %d: %s (broken %d, state %d, query %d)\n", \
                __LINE__, #condition, (link)->broken, (link)->ui.state, (link)->ui.q); \
            abort(); \
        } \
        (void)sc_wake_wait(bell, 10); \
        (void)sc_wake_drain(bell); \
        (void)link_poll(link); \
    } \
} while (0)

static void open_link(conn_link *l, budget_owner owner, const char *file, bool success) {
    conn_limits limits = {4, owner == BUDGET_META ? 8 : 32, 10000};
    assert(link_init(l, &pool, owner, generation++, limits, ring, &bell));
    assert(link_open_target(l, SC_DB_SQLITE, &text, str(file)));
    WAIT(l, l->ui.state != UI_CONN_CONNECTING, 10000);
    assert((l->ui.state == UI_CONN_CONNECTED) == success);
    if (!success) assert(l->fail_reason == PROTO_FAIL_LOCAL && l->failure.len > 0);
}

static void close_link(conn_link *l) {
    assert(link_close(l));
    WAIT(l, link_ended(l), 5000);
    assert(link_join(l));
    link_free(l);
}

static void run(conn_link *l, const char *sql) {
    assert(link_run(l, &text, str(sql)) && !l->refused);
    WAIT(l, !conn_ui_active(&l->ui), 10000);
}

static void cell(conn_link *l, uint64_t row, size_t column, const char *expected) {
    proto_cell c;
    bool found = result_cell(&l->res, row, column, &c);
    if (!found || c.null || c.truncated || c.len != strlen(expected) ||
        (c.bytes.len > 0 && memcmp(c.bytes.ptr, expected, c.bytes.len) != 0)) {
        fprintf(stderr, "SQLite cell (%llu,%zu) expected <%s>, got <%.*s>\n", (unsigned long long)row,
            column, expected, found ? (int)c.bytes.len : 0, found ? (const char *)c.bytes.ptr : "");
        abort();
    }
}

static uint64_t file_hash(const char *file) {
    FILE *f = fopen(file, "rb");
    assert(f != NULL);
    uint64_t h = UINT64_C(14695981039346656037);
    int c;
    while ((c = fgetc(f)) != EOF) h = (h ^ (uint8_t)c) * UINT64_C(1099511628211);
    assert(!ferror(f) && fclose(f) == 0);
    return h;
}

static void exec(sqlite3 *db, const char *sql) {
    char *message = NULL;
    int code = sqlite3_exec(db, sql, NULL, NULL, &message);
    if (code != SQLITE_OK) fprintf(stderr, "SQLite fixture: %s\n", message);
    sqlite3_free(message);
    assert(code == SQLITE_OK);
}

#include "explorer_local_catalog.h"

static void fixture(void) {
    assert(mkdtemp(directory) != NULL);
    snprintf(path, sizeof path, "%s/data # '日本.sqlite", directory);
    sqlite3 *db = NULL;
    assert(sqlite3_open(path, &db) == SQLITE_OK);
    exec(db, "CREATE TABLE sample(n INTEGER PRIMARY KEY, mixed, label TEXT);"
        "INSERT INTO sample VALUES (7,9223372036854775807,'seven'),(19,1.25,'nineteen'),"
        "(23,'',NULL),(37,x'0041FF','blob'),(41,NULL,'null');"
        "CREATE TABLE parent(a INTEGER,b TEXT,PRIMARY KEY(b,a)) WITHOUT ROWID;"
        "CREATE TABLE child(a INTEGER,b TEXT,g TEXT GENERATED ALWAYS AS (b || a) STORED,"
        "FOREIGN KEY(B,A) REFERENCES parent(b,a)); CREATE INDEX child_idx ON child(b,a);"
        "CREATE TABLE nullable_pk(a TEXT,b INTEGER,PRIMARY KEY(b,a));"
        "CREATE TABLE desc_pk(a INTEGER PRIMARY KEY DESC);"
        "CREATE VIEW sample_view AS SELECT * FROM sample;");
    exec(db, local_catalog_sql);
    char *long_sql = local_long_definition();
    exec(db, long_sql);
    free(long_sql);
    assert(sqlite3_close(db) == SQLITE_OK);
}

static void alter_catalog(void) {
    sqlite3 *db = NULL;
    assert(sqlite3_open(path, &db) == SQLITE_OK);
    exec(db, "PRAGMA journal_mode=DELETE; CREATE TABLE aaa(n INTEGER); DROP TABLE \"quote'\"\";日本\"");
    assert(sqlite3_close(db) == SQLITE_OK);
}

static uint64_t transaction(conn_link *l) {
    proto_header h;
    proto_reader r;
    assert(proto_read(link_arena(l, l->ui.terminal), &h, &r));
    uint64_t item, status;
    sc_str field;
    while (proto_get_u64(&r, &item) && item != PROTO_ITEM_END) {
        size_t fields = item == PROTO_ITEM_ERROR ? 4 : 1;
        for (size_t i = 0; i < fields; i++) assert(proto_get_str(&r, &field));
        if (item == PROTO_ITEM_ERROR) assert(proto_get_u64(&r, &status));
    }
    assert(item == PROTO_ITEM_END && proto_get_u64(&r, &status));
    return status;
}

static void values_and_batches(void) {
    run(&query, "SELECT mixed FROM sample ORDER BY n");
    assert(query.ui.q == Q_ROWS && query.res.rows == 5);
    cell(&query, 0, 0, "9223372036854775807");
    cell(&query, 1, 0, "1.25");
    cell(&query, 2, 0, "");
    cell(&query, 3, 0, "X'0041FF'");
    proto_cell c;
    assert(result_cell(&query.res, 4, 0, &c) && c.null);
    sc_str name;
    result_type type;
    assert(result_column(&query.res, 0, &name, &type) && !type.numeric && type.pg_oid == 0 &&
        type.label.len == strlen("unknown/mixed") && memcmp(type.label.ptr, "unknown/mixed", type.label.len) == 0);
    run(&query, "SELECT '',x'',NULL,CAST(x'80FF00' AS TEXT),printf('%05001d',1)");
    cell(&query, 0, 0, "");
    cell(&query, 0, 1, "X''");
    assert(result_cell(&query.res, 0, 2, &c) && c.null);
    assert(result_cell(&query.res, 0, 3, &c) && !c.null && !c.truncated && c.len == 3 &&
        c.bytes.ptr[0] == 0x80 && c.bytes.ptr[1] == 0xff && c.bytes.ptr[2] == 0);
    assert(result_cell(&query.res, 0, 4, &c) && c.truncated && c.len == 5001 && c.bytes.len == 4096);
    run(&query, "SELECT 1 WHERE false");
    assert(query.ui.q == Q_ROWS && query.res.rows == 0 && query.res.cols == 1);
    run(&query, "; -- skip\nSELECT 3; SELECT 29 AS last_value");
    assert(query.ui.sets == 2 && query.res.rows == 1);
    cell(&query, 0, 0, "29");
    run(&query, "BEGIN; SELECT 31; SELECT * FROM missing; SELECT 99");
    assert(query.ui.q == Q_ERROR_AFTER_ROWS && query.res.rows == 1);
    cell(&query, 0, 0, "31");
    assert(transaction(&query) == 2);
    run(&query, "ROLLBACK; SELECT 43");
    cell(&query, 0, 0, "43");
    run(&query, "SELECT '日本'; SELECT unknown_name");
    query_terminal terminal = query_terminal_read(link_arena(&query, query.ui.terminal));
    assert(terminal.error &&
        terminal.position == 21 && terminal.code.len >= 6 && memcmp(terminal.code.ptr, "SQLite", 6) == 0);
    puts("ok SQLite mixed storage classes, invalid text, exact integers, batches, native transaction and error position");
}

static void safety_and_cancel(void) {
    uint64_t before = file_hash(path);
    const char *denied[] = {"INSERT INTO sample VALUES(99,1,'bad')", "DROP TABLE sample",
        "CREATE TABLE bad(n)", "ATTACH ':memory:' AS other", "DETACH main", "PRAGMA query_only=OFF",
        "PRAGMA trusted_schema=ON", "PRAGMA writable_schema=ON", "PRAGMA journal_mode=WAL",
        "PRAGMA user_version=123", "SELECT load_extension('/tmp/missing')"};
    for (size_t i = 0; i < sizeof denied / sizeof *denied; i++) {
        run(&query, denied[i]);
        assert(query.ui.q == Q_ERROR && file_hash(path) == before);
    }
    char sentinel[256], sql[512];
    snprintf(sentinel, sizeof sentinel, "%s/sentinel.sqlite", directory);
    FILE *f = fopen(sentinel, "wb");
    assert(f != NULL && fputs("do not overwrite", f) >= 0 && fclose(f) == 0);
    uint64_t sentinel_before = file_hash(sentinel);
    snprintf(sql, sizeof sql, "VACUUM INTO '%s'", sentinel);
    run(&query, sql);
    assert(query.ui.q == Q_ERROR && file_hash(sentinel) == sentinel_before && file_hash(path) == before);
    assert(unlink(sentinel) == 0);
    run(&query, "SELECT name FROM pragma_table_xinfo('sample') ORDER BY cid");
    assert(query.ui.q == Q_ROWS && query.res.rows == 3);
    cell(&query, 0, 0, "n");
    const char *recursive = "WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<1000000000) SELECT sum(i) FROM n";
    for (size_t i = 0; i < 3; i++) {
        assert(link_run(&query, &text, str(recursive)));
        if (i != 0) sc_sleep_ms(20);
        assert(link_cancel(&query));
        WAIT(&query, !conn_ui_active(&query.ui), 5000);
        assert(query.ui.q == Q_CANCELLED);
        run(&query, "SELECT 47");
        cell(&query, 0, 0, "47");
    }
    run(&query, "WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<1000000000) SELECT i FROM n");
    assert(query.ui.q == Q_CAPPED && query.res.rows == 10000);
    cell(&query, 9999, 0, "10000");
    sqlite3 *writer = NULL;
    assert(sqlite3_open(path, &writer) == SQLITE_OK);
    exec(writer, "BEGIN EXCLUSIVE");
    assert(link_run(&query, &text, str("SELECT * FROM sample")));
    sc_sleep_ms(25);
    assert(conn_ui_active(&query.ui) && link_cancel(&query));
    WAIT(&query, !conn_ui_active(&query.ui), 5000);
    assert(query.ui.q == Q_CANCELLED);
    exec(writer, "ROLLBACK");
    assert(sqlite3_close(writer) == SQLITE_OK);
    run(&query, "SELECT count(*) FROM sample");
    cell(&query, 0, 0, "5");
    assert(file_hash(path) == before);
    puts("ok SQLite writes and policy changes denied, hashes unchanged, streaming cap and busy/recursive cancellation reusable");
}

static void wal(void) {
    sqlite3 *writer = NULL;
    assert(sqlite3_open(path, &writer) == SQLITE_OK);
    exec(writer, "PRAGMA journal_mode=WAL; PRAGMA wal_autocheckpoint=0; INSERT INTO sample VALUES(73,73,'wal-only')");
    char wal_path[300];
    snprintf(wal_path, sizeof wal_path, "%s-wal", path);
    assert(access(wal_path, F_OK) == 0);
    uint64_t before = file_hash(path), wal_before = file_hash(wal_path);
    open_link(&query, BUDGET_QUERY, path, true);
    open_link(&meta, BUDGET_META, path, true);
    run(&query, "SELECT label FROM sample WHERE n=73");
    cell(&query, 0, 0, "wal-only");
    close_link(&query);
    run(&meta, "SELECT count(*) FROM sample");
    cell(&meta, 0, 0, "6");
    close_link(&meta);
    assert(file_hash(path) == before && file_hash(wal_path) == wal_before);
    assert(sqlite3_close(writer) == SQLITE_OK);
    puts("ok SQLite live WAL committed row visible to independent read-only connections without checkpoint/recovery writes");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    sc_arena_init(&text, text_mem, sizeof text_mem);
    assert(sc_wake_new(&bell).ok && link_pool_init(&pool, budget_default_split()));
    fixture();
    open_link(&query, BUDGET_QUERY, path, true);
    values_and_batches();
    safety_and_cancel();
    close_link(&query);
    wal();
    char other[256];
    snprintf(other, sizeof other, "%s/missing.sqlite", directory);
    open_link(&query, BUDGET_QUERY, other, false);
    close_link(&query);
    assert(access(other, F_OK) != 0);
    snprintf(other, sizeof other, "%s/empty.sqlite", directory);
    FILE *f = fopen(other, "wb");
    assert(f != NULL && fclose(f) == 0);
    open_link(&query, BUDGET_QUERY, other, true);
    run(&query, "SELECT count(*) FROM sqlite_schema");
    cell(&query, 0, 0, "0");
    close_link(&query);
    struct stat st;
    assert(stat(other, &st) == 0 && st.st_size == 0 && unlink(other) == 0);
    snprintf(other, sizeof other, "%s/wrong.sqlite", directory);
    f = fopen(other, "wb");
    assert(f != NULL && fputs("not a database", f) >= 0 && fclose(f) == 0);
    uint64_t before = file_hash(other);
    open_link(&query, BUDGET_QUERY, other, false);
    close_link(&query);
    assert(file_hash(other) == before && unlink(other) == 0);
    assert(chmod(path, 0) == 0);
    open_link(&query, BUDGET_QUERY, path, false);
    close_link(&query);
    assert(chmod(path, 0600) == 0);
    open_link(&query, BUDGET_QUERY, directory, false);
    close_link(&query);
    local_catalog(SC_DB_SQLITE, alter_catalog);
    assert(link_pool_used(&pool) == 0);
    link_pool_free(&pool);
    sc_wake_free(&bell);
    assert(unlink(path) == 0 && rmdir(directory) == 0);
    puts("explorer_sqlite_integration: ok");
    return 0;
}
