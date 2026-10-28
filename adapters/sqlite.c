#define _POSIX_C_SOURCE 200809L
#include "sc_local.h"
#include <sqlite3.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { SQ_IDLE, SQ_PREPARE, SQ_STEP, SQ_NEXT, SQ_ERROR } sqlite_phase;

typedef struct {
    sc_local base;
    sqlite3 *db;
    sqlite3_stmt *statement;
    const char *tail;
    size_t offset;
    uint64_t busy_start;
    sqlite_phase phase;
} sqlite_connection;

static int interrupted(void *arg) {
    sqlite_connection *s = arg;
    return s->base.control != NULL && sc_db_control_cancelled(s->base.control);
}

static int busy(void *arg, int attempts) {
    sqlite_connection *s = arg;
    if (attempts == 0) s->busy_start = sc_clock_ms();
    if (interrupted(s) || sc_clock_ms() - s->busy_start >= 2000) return 0;
    sc_sleep_ms(5);
    return 1;
}

static int authorize(void *arg, int action, const char *one, const char *two,
                     const char *database, const char *source) {
    (void)arg;
    (void)database;
    (void)source;
    switch (action) {
    case SQLITE_SELECT:
    case SQLITE_READ:
    case SQLITE_TRANSACTION:
    case SQLITE_SAVEPOINT:
    case SQLITE_RECURSIVE:
        return SQLITE_OK;
    case SQLITE_FUNCTION:
        if (two == NULL || sqlite3_stricmp(two, "load_extension") == 0 ||
            sqlite3_stricmp(two, "readfile") == 0 || sqlite3_stricmp(two, "writefile") == 0)
            return SQLITE_DENY;
        return SQLITE_OK;
    case SQLITE_PRAGMA: {
        static const char *object[] = {"table_info", "table_xinfo", "table_list", "index_list",
            "index_info", "index_xinfo", "foreign_key_list"};
        static const char *plain[] = {"database_list", "compile_options", "collation_list",
            "page_count", "freelist_count", "encoding", "user_version", "schema_version",
            "data_version", "query_only", "trusted_schema", "integrity_check", "quick_check"};
        for (size_t i = 0; i < sizeof object / sizeof *object; i++)
            if (sqlite3_stricmp(one, object[i]) == 0) return SQLITE_OK;
        if (two == NULL) {
            for (size_t i = 0; i < sizeof plain / sizeof *plain; i++)
                if (sqlite3_stricmp(one, plain[i]) == 0) return SQLITE_OK;
        }
        return SQLITE_DENY;
    }
    default: return SQLITE_DENY;
    }
}

static sc_db_next_kind failed(sqlite_connection *s, int code) {
    char label[32];
    snprintf(label, sizeof label, "SQLite %d", code);
    sc_local_error(&s->base, label, sqlite3_errmsg(s->db));
    int offset = sqlite3_error_offset(s->db);
    s->base.position = 0;
    if (offset >= 0 && s->base.sql != NULL) {
        size_t end = s->offset + (size_t)offset;
        size_t len = strlen(s->base.sql);
        if (end <= len) {
            s->base.position = 1;
            for (size_t i = 0; i < end; i++)
                if (((uint8_t)s->base.sql[i] & 0xc0) != 0x80) s->base.position++;
        }
    }
    s->phase = SQ_ERROR;
    return SC_DB_ERROR;
}

sc_result_size sc_sqlite_open(sc_db *c, const sc_arena *a, sc_str path) {
    sqlite_connection *s = calloc(1, sizeof *s);
    if (s == NULL) return sc_err_size(ENOMEM);
    c->local = (uintptr_t)s;
    c->live = true;
    s->base.engine = SC_DB_SQLITE;
    s->base.ended = true;
    if (sqlite3_libversion_number() != 3054001 || sqlite3_threadsafe() == 0 ||
        !sqlite3_compileoption_used("OMIT_LOAD_EXTENSION")) {
        sc_local_error(&s->base, "SQLite", "Expected the pinned thread-safe SQLite 3.54.1 without loadable extensions.");
        return sc_ok_size(0);
    }
    if (!sc_local_path(&s->base, a, path)) return sc_ok_size(0);
    int code = sqlite3_open_v2(s->base.path, &s->db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL);
    if (code != SQLITE_OK) { (void)failed(s, code); return sc_ok_size(0); }
    int defensive = 0, trusted = 1;
    bool secure = sqlite3_db_config(s->db, SQLITE_DBCONFIG_DEFENSIVE, 1, &defensive) == SQLITE_OK && defensive == 1 &&
        sqlite3_db_config(s->db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, &trusted) == SQLITE_OK && trusted == 0 &&
        sqlite3_db_readonly(s->db, "main") == 1;
    if (!secure) {
        sc_local_error(&s->base, "SQLite", "The engine could not enforce the read-only access policy.");
        return sc_ok_size(0);
    }
    (void)sqlite3_extended_result_codes(s->db, 1);
    (void)sqlite3_limit(s->db, SQLITE_LIMIT_LENGTH, 16 * 1024 * 1024);
    (void)sqlite3_limit(s->db, SQLITE_LIMIT_SQL_LENGTH, 128 * 1024);
    (void)sqlite3_busy_handler(s->db, busy, s);
    sqlite3_progress_handler(s->db, 1000, interrupted, s);
    code = sqlite3_exec(s->db, "PRAGMA query_only=ON; PRAGMA temp_store=MEMORY;"
        "SELECT rootpage FROM main.sqlite_schema LIMIT 1", NULL, NULL, NULL);
    if (code != SQLITE_OK) { (void)failed(s, code); return sc_ok_size(0); }
    if (sqlite3_set_authorizer(s->db, authorize, s) != SQLITE_OK) {
        sc_local_error(&s->base, "SQLite", "Could not install the read-only authorizer.");
        return sc_ok_size(0);
    }
    s->base.ready = true;
    return sc_ok_size(0);
}

static void clear_statement(sqlite_connection *s) {
    (void)sqlite3_finalize(s->statement);
    s->statement = NULL;
    sc_local_clear_columns(&s->base);
}

static void end_query(sqlite_connection *s) {
    clear_statement(s);
    free(s->base.sql);
    s->base.sql = NULL;
    s->tail = NULL;
    s->base.ended = true;
    s->phase = SQ_IDLE;
}

sc_result_size sc_sqlite_send(sc_local *base, const sc_arena *a, sc_str sql) {
    sqlite_connection *s = (sqlite_connection *)base;
    end_query(s);
    base->error[0] = 0;
    base->position = 0;
    base->row_error = false;
    base->sql = sc_local_copy(a, sql);
    if (base->sql == NULL) return sc_err_size(EINVAL);
    base->ended = false;
    s->tail = base->sql;
    s->phase = SQ_PREPARE;
    return sc_ok_size(0);
}

static bool columns(sqlite_connection *s) {
    sc_local *base = &s->base;
    size_t n = (size_t)sqlite3_column_count(s->statement);
    if (n > SC_LOCAL_COLUMNS) {
        sc_local_error(base, "Explorer", "The result exceeds Explorer's 1,664-column limit.");
        return false;
    }
    base->columns = calloc(n == 0 ? 1 : n, sizeof *base->columns);
    if (base->columns == NULL) return false;
    base->cols = n;
    for (size_t i = 0; i < n; i++) {
        const char *declared = sqlite3_column_decltype(s->statement, (int)i);
        base->columns[i].name = strdup(sqlite3_column_name(s->statement, (int)i));
        base->columns[i].type = strdup(declared == NULL || declared[0] == 0 ? "unknown/mixed" : declared);
        if (base->columns[i].name == NULL || base->columns[i].type == NULL) return false;
    }
    return true;
}

sc_db_next_kind sc_sqlite_next(sc_local *base) {
    sqlite_connection *s = (sqlite_connection *)base;
    if (s->phase == SQ_ERROR) { end_query(s); return SC_DB_END; }
    if (s->phase == SQ_IDLE) return SC_DB_END;
    if (interrupted(s)) {
        sc_local_error(base, "SQLite 9", "Query interrupted");
        s->phase = SQ_ERROR;
        return SC_DB_ERROR;
    }
    if (s->phase == SQ_NEXT) { clear_statement(s); s->phase = SQ_PREPARE; }
    if (s->phase == SQ_PREPARE) {
        while (s->statement == NULL) {
            if (s->tail[0] == 0) { end_query(s); return SC_DB_END; }
            const char *current = s->tail;
            s->offset = (size_t)(current - base->sql);
            int code = sqlite3_prepare_v3(s->db, current, -1, 0, &s->statement, &s->tail);
            if (code != SQLITE_OK) return failed(s, code);
        }
        if (!columns(s)) {
            if (base->error[0] == 0) sc_local_error(base, "SQLite", "Could not represent the result's column metadata.");
            s->phase = SQ_ERROR;
            return SC_DB_ERROR;
        }
        s->phase = SQ_STEP;
    }
    int code = sqlite3_step(s->statement);
    base->transaction = sqlite3_get_autocommit(s->db) ? 0 : 2;
    if (code == SQLITE_ROW) { base->rows = 1; return SC_DB_ROWS; }
    base->rows = 0;
    if (code != SQLITE_DONE) return failed(s, code);
    snprintf(base->tag, sizeof base->tag, "%s", base->cols == 0 ? "OK" : "SELECT");
    s->phase = SQ_NEXT;
    return base->cols == 0 ? SC_DB_COMMAND : SC_DB_TUPLES;
}

bool sc_sqlite_row(sc_local *base, size_t row, size_t cap, const sc_arena *a, sc_buf *b) {
    sqlite_connection *s = (sqlite_connection *)base;
    if (row != 0 || base->rows != 1 || cap > 4096) return false;
    sc_buf before = *b;
    for (size_t i = 0; i < base->cols; i++) {
        int type = sqlite3_column_type(s->statement, (int)i);
        bool null = type == SQLITE_NULL;
        sc_local_text text = {.cap = cap};
        const void *bytes = type == SQLITE_BLOB ? sqlite3_column_blob(s->statement, (int)i) :
                                                 sqlite3_column_text(s->statement, (int)i);
        int len = sqlite3_column_bytes(s->statement, (int)i);
        if (!null && bytes == NULL && (len > 0 || type != SQLITE_BLOB)) {
            (void)failed(s, SQLITE_NOMEM);
            base->row_error = true;
            *b = before;
            return false;
        }
        if (!null) {
            if (type == SQLITE_BLOB) sc_local_text_hex(&text, bytes, (size_t)len);
            else sc_local_text_bytes(&text, bytes, (size_t)len);
        }
        if (!sc_local_cell(a, b, &text, null)) { *b = before; return false; }
    }
    return true;
}

sc_db_cancel_status sc_sqlite_cancel(sc_local *s) {
    if (s->control != NULL) sc_db_control_cancel(s->control, atomic_load(&s->control->request));
    return SC_DB_CANCEL_SENT;
}

void sc_sqlite_finish(sc_local *base) {
    sqlite_connection *s = (sqlite_connection *)base;
    end_query(s);
    (void)sqlite3_close(s->db);
    sc_local_free(base);
    free(s);
}
