#define _POSIX_C_SOURCE 200809L
#include "../../adapters/sc_db.h"
#include "../../examples/explorer/proto.h"
#include <duckdb.h>
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static sc_db db;
static sc_db_control control;
static char path[] = "/tmp/safe-c-duckdb-fuzz-XXXXXX";
static alignas(8) uint8_t memory[65536];
static sc_arena arena;
static uint64_t request;

static void finish(void) {
    sc_db_finish(&db);
    assert(unlink(path) == 0);
}

static void start(void) {
    int fd = mkstemp(path);
    assert(fd >= 0 && close(fd) == 0 && unlink(path) == 0);
    duckdb_database fixture = NULL;
    duckdb_config config = NULL;
    assert(duckdb_create_config(&config) == DuckDBSuccess);
    assert(duckdb_set_config(config, "threads", "2") == DuckDBSuccess);
    assert(duckdb_open_ext(path, &fixture, config, NULL) == DuckDBSuccess);
    duckdb_close(&fixture);
    duckdb_destroy_config(&config);
    sc_arena_init(&arena, memory, sizeof memory);
    sc_str file = sc_str_from_lit(&arena, (const uint8_t *)path, strlen(path));
    assert(sc_db_connect_start(&db, SC_DB_DUCKDB, &arena, file, file, SC_DB_QUERY).ok);
    bool write;
    assert(sc_db_connect_poll(db, false, &write) == SC_DB_CONNECTED);
    sc_db_control_init(&control);
    sc_db_set_control(db, &control);
    assert(atexit(finish) == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 10) return 0;
    if (!db.live) start();
    int64_t number;
    memcpy(&number, data, sizeof number);
    size_t bytes = size - 10;
    if (bytes > 512) bytes = 512;
    char hex[1025], blob[1028], integer[32], sql[4096];
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < bytes; i++) {
        hex[2 * i] = digits[data[i + 10] >> 4];
        hex[2 * i + 1] = digits[data[i + 10] & 15];
    }
    hex[bytes * 2] = 0;
    snprintf(blob, sizeof blob, "X'%s'", hex);
    snprintf(integer, sizeof integer, "%lld", (long long)number);
    size_t length = (size_t)data[8] * 23;
    bool null = (data[9] & 1) != 0;
    snprintf(sql, sizeof sql, "SELECT %s::BIGINT, from_hex('%s'), repeat('x',%zu), %s; SELECT 73",
             integer, hex, length, null ? "NULL" : "''");
    assert(sc_arena_reset(&arena));
    sc_str query = sc_str_from_lit(&arena, (const uint8_t *)sql, strlen(sql));
    sc_db_control_arm(&control, ++request);
    assert(sc_db_send_query(db, &arena, query).ok);
    assert(sc_arena_reset(&arena));
    assert(sc_db_next(db) == SC_DB_ROWS && sc_db_rows(db) == 1 && sc_db_columns(db) == 4);
    size_t cap = data[9] & 2 ? 4096 : data[8];
    sc_buf row = sc_buf_new(sc_arena_alloc(&arena, sizeof memory));
    assert(sc_db_put_row(db, 0, cap, &arena, &row));
    proto_reader r = {&arena, sc_buf_view(&arena, row), 0, false};
    const char *expected[] = {integer, blob};
    proto_cell cell;
    for (size_t i = 0; i < 2; i++) {
        size_t len = strlen(expected[i]), kept = len < cap ? len : cap;
        assert(proto_get_cell(&r, &cell) && !cell.null && cell.len == len &&
               cell.truncated == (kept < len) && cell.bytes.len == kept &&
               (kept == 0 || memcmp(cell.bytes.ptr, expected[i], kept) == 0));
    }
    assert(proto_get_cell(&r, &cell) && !cell.null && cell.len == length &&
           cell.truncated == (length > cap) && cell.bytes.len == (length < cap ? length : cap));
    for (size_t i = 0; i < cell.bytes.len; i++) assert(cell.bytes.ptr[i] == 'x');
    assert(proto_get_cell(&r, &cell) && cell.null == null && cell.len == 0 && !cell.truncated);
    assert(sc_db_next(db) == SC_DB_TUPLES && sc_db_next(db) == SC_DB_ROWS);
    assert(sc_arena_reset(&arena));
    row = sc_buf_new(sc_arena_alloc(&arena, sizeof memory));
    assert(sc_db_put_row(db, 0, 4096, &arena, &row));
    r = (proto_reader){&arena, sc_buf_view(&arena, row), 0, false};
    assert(proto_get_cell(&r, &cell) && cell.bytes.len == 2 && memcmp(cell.bytes.ptr, "73", 2) == 0);
    assert(sc_db_next(db) == SC_DB_TUPLES && sc_db_next(db) == SC_DB_END);
    return 0;
}
