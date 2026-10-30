#define _POSIX_C_SOURCE 200809L
#include "../examples/explorer/link.h"
#include <duckdb.h>
#include <assert.h>
#include <spawn.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static link_pool pool;
static sc_wake bell;
static conn_link query, meta;
static alignas(8) uint8_t text_mem[128u * 1024u];
static sc_arena text;
static char directory[] = "/tmp/safe-c-duckdb-test-XXXXXX";
static char path[256];
static uint32_t generation = 1;

static void writable(duckdb_database *db, const char *file) {
    duckdb_config config = NULL;
    assert(duckdb_create_config(&config) == DuckDBSuccess);
    assert(duckdb_set_config(config, "threads", "2") == DuckDBSuccess);
    assert(duckdb_open_ext(file, db, config, NULL) == DuckDBSuccess);
    duckdb_destroy_config(&config);
}

static sc_str str(const char *value) {
    assert(sc_arena_reset(&text));
    return sc_str_from_lit(&text, (const uint8_t *)value, strlen(value));
}

static void ring(void *arg) {
    (void)sc_wake_ring(*(sc_wake *)arg);
}

#define WAIT(link, condition, ms) do { \
    uint64_t until = sc_clock_ms() + (ms); \
    while (!(condition)) { \
        if (sc_clock_ms() >= until || (link)->broken) { \
            fprintf(stderr, "DuckDB wait failed at %d: %s (broken %d, state %d, query %d)\n", \
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
    assert(link_open_target(l, SC_DB_DUCKDB, &text, str(file)));
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
    if (l->ui.q == Q_ERROR || l->ui.q == Q_ERROR_AFTER_ROWS) {
        proto_header h;
        proto_reader r;
        assert(proto_read(link_arena(l, l->ui.terminal), &h, &r));
        uint64_t item;
        sc_str value;
        while (proto_get_u64(&r, &item) && item != PROTO_ITEM_END) {
            assert(proto_get_str(&r, &value));
            if (item == PROTO_ITEM_ERROR) {
                assert(proto_get_str(&r, &value));
                fprintf(stderr, "DuckDB error: %.*s\n", (int)value.len, value.ptr);
                break;
            }
        }
    }
}

static void cell(conn_link *l, uint64_t row, size_t column, const char *expected) {
    proto_cell c = {0};
    bool found = result_cell(&l->res, row, column, &c);
    if (!found || c.null || c.truncated || c.len != strlen(expected) ||
        (c.bytes.len > 0 && memcmp(c.bytes.ptr, expected, c.bytes.len) != 0))
        fprintf(stderr, "cell (%llu,%zu) expected <%s>, got <%.*s>, length %llu, null %d, truncated %d\n",
                (unsigned long long)row, column, expected, (int)c.bytes.len,
                c.bytes.len == 0 ? "" : (const char *)c.bytes.ptr, (unsigned long long)c.len, c.null, c.truncated);
    assert(found && !c.null && !c.truncated &&
           c.len == strlen(expected) && (c.bytes.len == 0 || memcmp(c.bytes.ptr, expected, c.bytes.len) == 0));
}

#include "explorer_local_catalog.h"

static void fixture(void) {
    assert(mkdtemp(directory) != NULL);
    snprintf(path, sizeof path, "%s/data # '\"日本.duckdb", directory);
    duckdb_database db = NULL;
    duckdb_connection c = NULL;
    duckdb_result result = {0};
    writable(&db, path);
    assert(duckdb_connect(db, &c) == DuckDBSuccess);
    assert(duckdb_query(c, "CREATE TABLE sample(n INTEGER PRIMARY KEY, label VARCHAR);"
                           "INSERT INTO sample VALUES (7, 'seven'), (19, 'nineteen');"
                           "CREATE SCHEMA other; CREATE TABLE other.sample(n INTEGER);"
                           "CREATE TABLE parent(a INTEGER,b INTEGER,PRIMARY KEY(b,a),UNIQUE(a));"
                           "CREATE TABLE child(a INTEGER,b INTEGER,g INTEGER GENERATED ALWAYS AS (a+b),"
                           "FOREIGN KEY(b,a) REFERENCES parent(b,a)); CREATE INDEX child_idx ON child(b,a);"
                           "CREATE TYPE nul_enum AS ENUM (SELECT 'a' || chr(0) || 'b' UNION ALL "
                           "SELECT repeat('z',5001) UNION ALL SELECT '日本\\back''quote');"
                           "CREATE TABLE nul_values AS SELECT ('a' || chr(0) || 'b')::nul_enum AS value;"
                           "CREATE TABLE native_values AS SELECT '101'::BIT AS bits, "
                           "'-12345678901234567890123456789012345678901234567890'::BIGNUM AS big, "
                           "{'n':42,'s':'a'||chr(0)||'b'}::VARIANT AS variant;"
                           "CREATE VIEW sample_view AS SELECT * FROM sample", &result) == DuckDBSuccess);
    duckdb_destroy_result(&result);
    assert(duckdb_query(c, local_catalog_sql, &result) == DuckDBSuccess);
    duckdb_destroy_result(&result);
    char *long_sql = local_long_definition();
    assert(duckdb_query(c, long_sql, &result) == DuckDBSuccess);
    free(long_sql);
    duckdb_destroy_result(&result);
    duckdb_disconnect(&c);
    duckdb_close(&db);
}

static void alter_catalog(void) {
    duckdb_database db = NULL;
    duckdb_connection c = NULL;
    duckdb_result result = {0};
    writable(&db, path);
    assert(duckdb_connect(db, &c) == DuckDBSuccess);
    assert(duckdb_query(c, "CREATE TABLE aaa(n INTEGER); DROP TABLE \"quote'\"\";日本\"", &result) == DuckDBSuccess);
    duckdb_destroy_result(&result);
    duckdb_disconnect(&c);
    duckdb_close(&db);
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

static void native_values(void) {
    run(&query, "SELECT '101'::BIT, 123::BIGNUM, 42::VARIANT");
    cell(&query, 0, 0, "101");
    cell(&query, 0, 1, "123");
    cell(&query, 0, 2, "42");
    run(&query, "SELECT * FROM native_values");
    cell(&query, 0, 0, "101");
    cell(&query, 0, 1, "-12345678901234567890123456789012345678901234567890");
    const char variant[] = "{'n': 42, 's': a\0b}";
    proto_cell c;
    assert(result_cell(&query.res, 0, 2, &c) && !c.null && !c.truncated &&
           c.len == sizeof variant - 1 && c.bytes.len == sizeof variant - 1 &&
           memcmp(c.bytes.ptr, variant, sizeof variant - 1) == 0);
    run(&query, "SELECT ['100000001'::BIT,NULL], {'v': 123::BIGNUM}, [42::VARIANT], "
                "NULL::BIT,NULL::BIGNUM,NULL::VARIANT, repeat('1',5001)::BIT, "
                "('-'||repeat('9',5001))::BIGNUM, {'s':repeat('z',5001)}::VARIANT");
    cell(&query, 0, 0, "[100000001, NULL]");
    cell(&query, 0, 1, "{'v': 123}");
    cell(&query, 0, 2, "[42]");
    for (size_t i = 3; i < 6; i++) assert(result_cell(&query.res, 0, i, &c) && c.null);
    const size_t lengths[] = {5001, 5002, 5008};
    const char first[] = {'1', '-', '{'};
    for (size_t i = 0; i < 3; i++) {
        assert(result_cell(&query.res, 0, i + 6, &c) && !c.null && c.truncated &&
               c.len == lengths[i] && c.bytes.len == 4096 && c.bytes.ptr[0] == first[i]);
    }
    run(&query, "SELECT 71");
    cell(&query, 0, 0, "71");
    puts("ok DuckDB BIT/BIGNUM/VARIANT scalar, stored, nested, NULL and truncated values; query reuse");
}

static void values_and_batches(void) {
    run(&query, "SELECT n, label FROM sample ORDER BY n");
    assert(query.ui.q == Q_ROWS && query.res.rows == 2);
    cell(&query, 0, 0, "7");
    cell(&query, 1, 1, "nineteen");
    sc_str name;
    result_type type;
    assert(result_column(&query.res, 0, &name, &type) && type.numeric && type.pg_oid == 0 &&
           type.label.len == 7 && memcmp(type.label.ptr, "INTEGER", 7) == 0);
    run(&query, "SELECT 1::INTEGER WHERE false");
    assert(query.ui.q == Q_ROWS && query.res.cols == 1 && query.res.rows == 0);
    run(&query, "SELECT 1; SELECT 27 AS last_value");
    assert(query.ui.q == Q_ROWS && query.res.rows == 1 && query.ui.sets == 2);
    cell(&query, 0, 0, "27");
    run(&query, "SELECT 37; SELECT * FROM missing_table; SELECT 99");
    assert(query.ui.q == Q_ERROR_AFTER_ROWS && query.res.rows == 1);
    cell(&query, 0, 0, "37");
    run(&query, "SELECT 123456789012345678901234567890.1234567::DECIMAL(38,7),"
                "'-170141183460469231731687303715884105728'::HUGEINT,"
                "340282366920938463463374607431768211455::UHUGEINT,"
                "NULL, '', ''::BLOB, from_hex('0041FF'), [1::HUGEINT, NULL, -7],"
                "{'x': 12345678901234567890::HUGEINT, 'label': 'a''b'},"
                "TIMESTAMP_NS '2026-10-28 01:02:03.123456789', DATE '1999-12-31',"
                "'00112233-4455-6677-8899-aabbccddeeff'::UUID, repeat('z',5001)");
    assert(query.ui.q == Q_ROWS && query.res.rows == 1);
    cell(&query, 0, 0, "123456789012345678901234567890.1234567");
    cell(&query, 0, 1, "-170141183460469231731687303715884105728");
    cell(&query, 0, 2, "340282366920938463463374607431768211455");
    proto_cell c;
    assert(result_cell(&query.res, 0, 3, &c) && c.null && c.len == 0);
    cell(&query, 0, 4, "");
    cell(&query, 0, 5, "X''");
    cell(&query, 0, 6, "X'0041FF'");
    cell(&query, 0, 7, "[1, NULL, -7]");
    cell(&query, 0, 8, "{'x': 12345678901234567890, 'label': 'a''b'}");
    cell(&query, 0, 9, "2026-10-28 01:02:03.123456789");
    cell(&query, 0, 10, "1999-12-31");
    cell(&query, 0, 11, "00112233-4455-6677-8899-aabbccddeeff");
    assert(result_cell(&query.res, 0, 12, &c) && !c.null && c.truncated && c.len == 5001 &&
           c.bytes.len == 4096 && c.bytes.ptr[4095] == 'z');
    run(&query, "SELECT value, [value], {'v': value}, '日本\\back''quote'::nul_enum, "
                "repeat('z',5001)::nul_enum FROM nul_values");
    static const char *const bytes[] = {"a\0b", "['a\0b']", "{'v': 'a\0b'}"};
    static const size_t lengths[] = {3, 7, 12};
    for (size_t i = 0; i < 3; i++) {
        assert(result_cell(&query.res, 0, i, &c) && !c.null && !c.truncated &&
               c.len == lengths[i] && c.bytes.len == lengths[i] &&
               memcmp(c.bytes.ptr, bytes[i], lengths[i]) == 0);
    }
    cell(&query, 0, 3, "日本\\back'quote");
    assert(result_cell(&query.res, 0, 4, &c) && c.truncated && c.len == 5001 && c.bytes.len == 4096);
    puts("ok DuckDB exact scalar/nested values, native labels, NULL/blob/text distinctions and batches");
}

static void caps_and_cancel(void) {
    run(&query, "SELECT i FROM range(1000000000) t(i)");
    assert(query.ui.q == Q_CAPPED && query.res.rows == 10000);
    cell(&query, 9999, 0, "9999");
    for (size_t attempt = 0; attempt < 4; attempt++) {
        const char *sql = "SELECT sum(a.i*b.i) FROM range(1000000000) a(i), range(1000000000) b(i)";
        assert(link_run(&query, &text, str(sql)));
        if (attempt != 0) sc_sleep_ms(20);
        uint64_t start = sc_clock_ms();
        assert(link_cancel(&query));
        WAIT(&query, !conn_ui_active(&query.ui), 5000);
        assert(query.ui.q == Q_CANCELLED && sc_clock_ms() - start < 5000);
        run(&query, "SELECT 43");
        cell(&query, 0, 0, "43");
    }
    assert(link_run_limit(&query, &text,
           str("SELECT i, repeat('x',100) FROM range(1000000000) t(i)"), UINT64_MAX));
    WAIT(&query, query.res.rows > 0, 5000);
    assert(conn_ui_active(&query.ui) && link_cancel(&query));
    WAIT(&query, !conn_ui_active(&query.ui), 5000);
    assert(query.ui.q == Q_CANCELLED && query.res.rows > 0);
    assert(link_run_limit(&query, &text,
           str("SELECT i, repeat('x',100) FROM range(1000000000) t(i)"), UINT64_MAX));
    sc_sleep_ms(100);
    assert(link_cancel(&query));
    WAIT(&query, !conn_ui_active(&query.ui), 5000);
    assert(query.ui.q == Q_CANCELLED);
    run(&query, "SELECT 47");
    cell(&query, 0, 0, "47");
    puts("ok DuckDB billion-row stream caps; cancel before first row, during fetch and without credits; next query succeeds");
}

static void safety(void) {
    uint64_t before = file_hash(path);
    const char *denied[] = {
        "INSERT INTO sample VALUES (99,'bad')", "DROP TABLE sample", "CREATE TABLE bad(n INTEGER)",
        "ATTACH ':memory:' AS other_db", "SET enable_external_access=true", "SET lock_configuration=false",
        "SET autoload_known_extensions=true", "INSTALL httpfs", "LOAD httpfs",
        "SELECT * FROM read_csv('/etc/passwd')"
    };
    for (size_t i = 0; i < sizeof denied / sizeof *denied; i++) {
        run(&query, denied[i]);
        assert(query.ui.q == Q_ERROR && file_hash(path) == before);
    }
    char sentinel[256], sql[512];
    snprintf(sentinel, sizeof sentinel, "%s/sentinel.csv", directory);
    FILE *f = fopen(sentinel, "wb");
    assert(f != NULL && fputs("do not overwrite", f) >= 0 && fclose(f) == 0);
    uint64_t sentinel_before = file_hash(sentinel);
    snprintf(sql, sizeof sql, "COPY sample TO '%s'", sentinel);
    run(&query, sql);
    assert(query.ui.q == Q_ERROR && file_hash(sentinel) == sentinel_before && file_hash(path) == before);
    assert(unlink(sentinel) == 0);
    run(&query, "SELECT count(*) FROM sample");
    cell(&query, 0, 0, "2");
    puts("ok DuckDB writes, external reads, exports, attachments, extensions and policy changes denied; file hashes unchanged");
}

static void wide_and_memory(void) {
    char sql[32768];
    size_t used = (size_t)snprintf(sql, sizeof sql, "SELECT 1");
    for (size_t i = 0; i < RESULT_MAX_COLUMNS; i++)
        used += (size_t)snprintf(sql + used, sizeof sql - used, ",1");
    run(&query, sql);
    assert(query.ui.q == Q_ERROR && !query.broken);
    run(&query, "SELECT i, repeat('x',64) || i::VARCHAR FROM range(2000000) t(i) ORDER BY i DESC");
    assert(query.ui.q == Q_CAPPED || query.ui.q == Q_ERROR);
    if (query.ui.q == Q_CAPPED) {
        cell(&query, 0, 0, "1999999");
        cell(&query, 9999, 0, "1990000");
    }
    run(&query, "SELECT 59");
    cell(&query, 0, 0, "59");
    puts("ok DuckDB oversized schema is a recoverable error; bounded-memory sort completes or reports engine exhaustion");
}

static void retained_and_alias(void) {
    char alias[256];
    snprintf(alias, sizeof alias, "%s/alias.duckdb", directory);
    assert(symlink(path, alias) == 0);
    open_link(&query, BUDGET_QUERY, alias, true);
    assert(query.conninfo.len == strlen(path));
    static alignas(8) uint8_t book_mem[65536];
    sc_arena book;
    sc_arena_init(&book, book_mem, sizeof book_mem);
    result kept;
    assert(result_init(&kept, &book, 32));
    link_adopt(&query, kept);
    run(&query, "SELECT 61 AS retained");
    conn_ui shown = query.ui;
    assert(link_keep(&query, &kept));
    close_link(&query);
    proto_cell c;
    assert(result_cell(&kept, 0, 0, &c) && c.bytes.len == 2 && memcmp(c.bytes.ptr, "61", 2) == 0);
    sc_tslice dropped = SC_TSLICE_ALLOC(size_t, &book, 33);
    size_t count = result_close(&kept, &book, dropped);
    for (size_t i = 0; i < count; i++) {
        size_t index;
        assert(SC_TSLICE_GET(size_t, &book, dropped, i, &index) && link_pool_free_kept(&pool, index));
    }
    assert(shown.terminal_kept && link_pool_free_kept(&pool, shown.terminal));
    assert(unlink(alias) == 0);
    run(&meta, "SELECT 67");
    cell(&meta, 0, 0, "67");
    puts("ok DuckDB canonical symlink identity, retained rows after worker release and independent metadata connection");
}

static int hold_lock(char **argv) {
    duckdb_database db = NULL;
    writable(&db, argv[2]);
    char byte = 'r';
    assert(write(atoi(argv[3]), &byte, 1) == 1);
    assert(read(atoi(argv[4]), &byte, 1) == 1);
    duckdb_close(&db);
    return 0;
}

static void locked_file(const char *self) {
    int ready[2], release[2];
    assert(pipe(ready) == 0 && pipe(release) == 0);
    char ready_fd[24], release_fd[24];
    snprintf(ready_fd, sizeof ready_fd, "%d", ready[1]);
    snprintf(release_fd, sizeof release_fd, "%d", release[0]);
    pid_t child;
    char *args[] = {(char *)self, "--lock", path, ready_fd, release_fd, NULL};
    assert(posix_spawn(&child, self, NULL, NULL, args, environ) == 0);
    assert(close(ready[1]) == 0 && close(release[0]) == 0);
    char byte;
    assert(read(ready[0], &byte, 1) == 1);
    uint64_t before = file_hash(path);
    open_link(&query, BUDGET_QUERY, path, false);
    close_link(&query);
    assert(file_hash(path) == before);
    assert(write(release[1], &byte, 1) == 1);
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(close(ready[0]) == 0 && close(release[1]) == 0);
    puts("ok DuckDB file locked by a writer fails without a writable retry or file changes");
}

int main(int argc, char **argv) {
    if (argc == 5 && strcmp(argv[1], "--lock") == 0) return hold_lock(argv);
    setvbuf(stdout, NULL, _IONBF, 0);
    sc_arena_init(&text, text_mem, sizeof text_mem);
    assert(sc_wake_new(&bell).ok && link_pool_init(&pool, budget_default_split()));
    fixture();
    locked_file(argv[0]);
    open_link(&query, BUDGET_QUERY, path, true);
    open_link(&meta, BUDGET_META, path, true);
    native_values();
    values_and_batches();
    caps_and_cancel();
    safety();
    wide_and_memory();
    assert(link_run(&query, &text, str("SELECT sum(a.i*b.i) FROM range(1000000000) a(i), range(1000000000) b(i)")));
    sc_sleep_ms(20);
    close_link(&query);
    run(&meta, "SELECT 53");
    cell(&meta, 0, 0, "53");
    retained_and_alias();
    close_link(&meta);
    char missing[256];
    snprintf(missing, sizeof missing, "%s/missing.duckdb", directory);
    open_link(&query, BUDGET_QUERY, missing, false);
    close_link(&query);
    assert(access(missing, F_OK) != 0);
    open_link(&query, BUDGET_QUERY, directory, false);
    close_link(&query);
    open_link(&query, BUDGET_QUERY, ":memory:", false);
    close_link(&query);
    snprintf(missing, sizeof missing, "%s/wrong.duckdb", directory);
    FILE *wrong = fopen(missing, "wb");
    assert(wrong != NULL && fputs("not a database", wrong) >= 0 && fclose(wrong) == 0);
    uint64_t before = file_hash(missing);
    open_link(&query, BUDGET_QUERY, missing, false);
    close_link(&query);
    assert(file_hash(missing) == before && unlink(missing) == 0);
    local_catalog(SC_DB_DUCKDB, alter_catalog);
    assert(link_pool_used(&pool) == 0);
    link_pool_free(&pool);
    sc_wake_free(&bell);
    assert(unlink(path) == 0 && rmdir(directory) == 0);
    struct rusage usage;
    assert(getrusage(RUSAGE_SELF, &usage) == 0);
    printf("explorer_duckdb_integration: ok (peak RSS %ld platform units; SDK engine is not instrumented)\n", usage.ru_maxrss);
    return 0;
}
