#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define DUCKDB_API_ALLOW_DEPRECATED 1
#include "sc_duckdb_format.h"
#include <errno.h>
#include <ftw.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct duck_session {
    struct duck_session *next;
    char *path;
    char spill[64];
    size_t refs;
    duckdb_database db;
} duck_session;

typedef enum { DD_IDLE, DD_PREPARE, DD_FETCH, DD_NEXT, DD_ERROR } duck_phase;

typedef struct {
    sc_local base;
    duck_session *session;
    duckdb_connection connection;
    duckdb_extracted_statements statements;
    duckdb_prepared_statement prepared;
    duckdb_result result;
    duckdb_data_chunk chunk;
    size_t statement, count;
    duck_phase phase;
    pthread_mutex_t interrupt_lock;
    sc_thread interrupter;
    bool active, stop;
} duck_connection;

static duck_session *sessions;
static pthread_mutex_t sessions_lock = PTHREAD_MUTEX_INITIALIZER;

static int spill_unlink(const char *path, const struct stat *st, int type, struct FTW *ftw) {
    (void)st;
    (void)type;
    (void)ftw;
    return remove(path);
}

static void spill_remove(const char *path) {
    if (path != NULL && path[0] != 0)
        (void)nftw(path, spill_unlink, 16, FTW_DEPTH | FTW_PHYS);
}

static duck_session *retain_session(sc_local *s) {
    pthread_mutex_lock(&sessions_lock);
    for (duck_session *p = sessions; p != NULL; p = p->next) {
        if (strcmp(p->path, s->path) == 0) {
            p->refs++;
            pthread_mutex_unlock(&sessions_lock);
            return p;
        }
    }
    duck_session *p = calloc(1, sizeof *p);
    duckdb_config config = NULL;
    char *error = NULL;
    bool ok = p != NULL;
    if (ok) {
        p->path = strdup(s->path);
        snprintf(p->spill, sizeof p->spill, "/tmp/safe-c-duckdb-XXXXXX");
        ok = p->path != NULL && mkdtemp(p->spill) != NULL &&
             duckdb_create_config(&config) == DuckDBSuccess;
    }
    if (ok) {
        const char *keys[] = {"access_mode", "temp_directory", "enable_external_access",
            "autoload_known_extensions", "autoinstall_known_extensions", "allow_unsigned_extensions",
            "memory_limit", "threads", "max_temp_directory_size", "lock_configuration"};
        const char *values[] = {"READ_ONLY", p->spill, "false", "false", "false", "false",
                                "128MB", "1", "64MB", "true"};
        for (size_t i = 0; ok && i < sizeof keys / sizeof *keys; i++)
            ok = duckdb_set_config(config, keys[i], values[i]) == DuckDBSuccess;
        if (!ok) sc_local_error(s, "DuckDB", "The engine could not enforce the read-only access policy.");
    }
    if (ok) ok = duckdb_open_ext(s->path, &p->db, config, &error) == DuckDBSuccess;
    if (!ok) {
        if (error != NULL) sc_local_error(s, "DuckDB", error);
        else if (s->error[0] == 0) sc_local_error(s, "DuckDB", "Could not allocate the database session.");
        if (p != NULL) {
            duckdb_close(&p->db);
            spill_remove(p->spill);
            free(p->path);
            free(p);
            p = NULL;
        }
    } else {
        p->refs = 1;
        p->next = sessions;
        sessions = p;
    }
    duckdb_free(error);
    duckdb_destroy_config(&config);
    pthread_mutex_unlock(&sessions_lock);
    return p;
}

static void release_session(duck_session *session) {
    if (session == NULL) return;
    pthread_mutex_lock(&sessions_lock);
    if (--session->refs == 0) {
        duck_session **p = &sessions;
        while (*p != session) p = &(*p)->next;
        *p = session->next;
        duckdb_close(&session->db);
        spill_remove(session->spill);
        free(session->path);
        free(session);
    }
    pthread_mutex_unlock(&sessions_lock);
}

static void interrupt_main(void *arg) {
    duck_connection *d = arg;
    for (;;) {
        pthread_mutex_lock(&d->interrupt_lock);
        bool stop = d->stop;
        if (!stop && d->active && d->base.control != NULL &&
            sc_db_control_cancelled(d->base.control)) duckdb_interrupt(d->connection);
        pthread_mutex_unlock(&d->interrupt_lock);
        if (stop) return;
        sc_sleep_ms(1);
    }
}

sc_result_size sc_duckdb_open(sc_db *c, const sc_arena *a, sc_str path) {
    duck_connection *d = calloc(1, sizeof *d);
    if (d == NULL) return sc_err_size(ENOMEM);
    if (pthread_mutex_init(&d->interrupt_lock, NULL) != 0) { free(d); return sc_err_size(ENOMEM); }
    c->local = (uintptr_t)d;
    c->live = true;
    d->base.engine = SC_DB_DUCKDB;
    d->base.transaction = 4;
    d->base.ended = true;
    if (strcmp(duckdb_library_version(), "v2.0.0") != 0) {
        sc_local_error(&d->base, "DuckDB", "Expected the pinned DuckDB 2.0.0 C SDK.");
        return sc_ok_size(0);
    }
    if (!sc_local_path(&d->base, a, path)) return sc_ok_size(0);
    d->session = retain_session(&d->base);
    if (d->session == NULL) return sc_ok_size(0);
    if (duckdb_connect(d->session->db, &d->connection) != DuckDBSuccess ||
        !sc_thread_start(&d->interrupter, interrupt_main, d, 1u << 20).ok) {
        sc_local_error(&d->base, "DuckDB", "Could not start the database connection.");
        return sc_ok_size(0);
    }
    d->base.ready = true;
    return sc_ok_size(0);
}

static void clear_statement(duck_connection *d) {
    duckdb_destroy_data_chunk(&d->chunk);
    duckdb_destroy_result(&d->result);
    duckdb_destroy_prepare(&d->prepared);
    sc_local_clear_columns(&d->base);
}

static void end_query(duck_connection *d) {
    clear_statement(d);
    duckdb_destroy_extracted(&d->statements);
    pthread_mutex_lock(&d->interrupt_lock);
    d->active = false;
    pthread_mutex_unlock(&d->interrupt_lock);
    free(d->base.sql);
    d->base.sql = NULL;
    d->base.ended = true;
    d->phase = DD_IDLE;
}

static sc_db_next_kind failed(duck_connection *d, const char *message) {
    char code[32];
    snprintf(code, sizeof code, "DuckDB %u", (unsigned)duckdb_result_error_type(&d->result));
    sc_local_error(&d->base, code, message);
    d->phase = DD_ERROR;
    return SC_DB_ERROR;
}

sc_result_size sc_duckdb_send(sc_local *s, const sc_arena *a, sc_str sql) {
    duck_connection *d = (duck_connection *)s;
    end_query(d);
    s->error[0] = 0;
    s->row_error = false;
    s->sql = sc_local_copy(a, sql);
    if (s->sql == NULL) return sc_err_size(EINVAL);
    s->ended = false;
    pthread_mutex_lock(&d->interrupt_lock);
    d->active = true;
    pthread_mutex_unlock(&d->interrupt_lock);
    d->statement = 0;
    d->count = duckdb_extract_statements(d->connection, s->sql, &d->statements);
    d->phase = DD_PREPARE;
    return sc_ok_size(0);
}

static bool columns(duck_connection *d) {
    sc_local *s = &d->base;
    size_t n = duckdb_column_count(&d->result);
    if (sc_local_reject_wide_schema(s, n)) return false;
    s->columns = calloc(n == 0 ? 1 : n, sizeof *s->columns);
    if (s->columns == NULL) return false;
    s->cols = n;
    for (size_t i = 0; i < n; i++) {
        duckdb_logical_type type = duckdb_column_logical_type(&d->result, i);
        const char *name = duckdb_column_name(&d->result, i);
        s->columns[i].name = name == NULL ? NULL : strdup(name);
        s->columns[i].type = sc_duckdb_type_label(type);
        s->columns[i].numeric = sc_duckdb_type_numeric(type);
        duckdb_destroy_logical_type(&type);
        if (s->columns[i].name == NULL || s->columns[i].type == NULL) return false;
    }
    return true;
}

sc_db_next_kind sc_duckdb_next(sc_local *s) {
    duck_connection *d = (duck_connection *)s;
    if (d->phase == DD_ERROR) { end_query(d); return SC_DB_END; }
    if (d->phase == DD_IDLE) return SC_DB_END;
    if (s->control != NULL && sc_db_control_cancelled(s->control)) return failed(d, "Query interrupted");
    if (d->phase == DD_NEXT) { clear_statement(d); d->phase = DD_PREPARE; }
    if (d->phase == DD_PREPARE) {
        const char *parse_error = duckdb_extract_statements_error(d->statements);
        if (parse_error != NULL && parse_error[0] != 0) return failed(d, parse_error);
        if (d->statement == d->count) { end_query(d); return SC_DB_END; }
        if (duckdb_prepare_extracted_statement(d->connection, d->statements, d->statement++, &d->prepared) != DuckDBSuccess)
            return failed(d, duckdb_prepare_error(d->prepared));
        if (duckdb_execute_prepared_streaming(d->prepared, &d->result) != DuckDBSuccess)
            return failed(d, duckdb_result_error(&d->result));
        if (duckdb_result_return_type(d->result) != DUCKDB_RESULT_TYPE_QUERY_RESULT) {
            snprintf(s->tag, sizeof s->tag, "OK");
            d->phase = DD_NEXT;
            return SC_DB_COMMAND;
        }
        if (!columns(d)) {
            if (s->error[0] == 0) sc_local_error(s, "DuckDB", "Could not represent the result's column metadata.");
            d->phase = DD_ERROR;
            return SC_DB_ERROR;
        }
        snprintf(s->tag, sizeof s->tag, "SELECT");
        d->phase = DD_FETCH;
    }
    duckdb_destroy_data_chunk(&d->chunk);
    d->chunk = duckdb_fetch_chunk(d->result);
    s->rows = d->chunk == NULL ? 0 : duckdb_data_chunk_get_size(d->chunk);
    if (d->chunk == NULL) {
        const char *error = duckdb_result_error(&d->result);
        if (error != NULL && error[0] != 0) return failed(d, error);
        d->phase = DD_NEXT;
        return SC_DB_TUPLES;
    }
    return SC_DB_ROWS;
}

bool sc_duckdb_row(sc_local *s, size_t row, size_t cap, const sc_arena *a, sc_buf *b) {
    duck_connection *d = (duck_connection *)s;
    if (row >= s->rows || cap > 4096) return false;
    sc_buf before = *b;
    for (size_t i = 0; i < s->cols; i++) {
        duckdb_vector vector = duckdb_data_chunk_get_vector(d->chunk, i);
        bool null = !duckdb_validity_row_is_valid(duckdb_vector_get_validity(vector), row);
        sc_local_text text = {.cap = cap, .control = s->control};
        if (!null && !sc_duckdb_format(&text, vector, row)) {
            (void)failed(d, "Could not format the value (unsupported type, nesting limit, or cancellation).");
            s->row_error = true;
            *b = before;
            return false;
        }
        if (!sc_local_cell(a, b, &text, null)) {
            *b = before;
            return false;
        }
    }
    return true;
}

sc_db_cancel_status sc_duckdb_cancel(sc_local *s) {
    duck_connection *d = (duck_connection *)s;
    if (s->control != NULL) sc_db_control_cancel(s->control, atomic_load(&s->control->request));
    pthread_mutex_lock(&d->interrupt_lock);
    if (d->active) duckdb_interrupt(d->connection);
    pthread_mutex_unlock(&d->interrupt_lock);
    return SC_DB_CANCEL_SENT;
}

void sc_duckdb_finish(sc_local *s) {
    duck_connection *d = (duck_connection *)s;
    end_query(d);
    pthread_mutex_lock(&d->interrupt_lock);
    d->stop = true;
    pthread_mutex_unlock(&d->interrupt_lock);
    if (d->interrupter.running) (void)sc_thread_join(&d->interrupter);
    duckdb_disconnect(&d->connection);
    release_session(d->session);
    pthread_mutex_destroy(&d->interrupt_lock);
    sc_local_free(s);
    free(d);
}
