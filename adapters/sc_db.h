#ifndef SAFEC_ADAPTER_DB_H
#define SAFEC_ADAPTER_DB_H

#include "sc_pq.h"

typedef enum { SC_DB_POSTGRESQL, SC_DB_DUCKDB, SC_DB_SQLITE } sc_db_engine;

typedef struct {
    atomic_uint_fast64_t request;
    atomic_uint_fast64_t cancelled;
    atomic_bool closing;
} sc_db_control;

typedef struct {
    sc_db_engine engine;
    sc_pq pg;
    uintptr_t local;
    bool live;
} sc_db;

typedef enum {
    SC_DB_CONNECTING, SC_DB_CONNECTED, SC_DB_FAILED_AUTH, SC_DB_NEEDS_PASSWORD,
    SC_DB_FAILED_NETWORK
} sc_db_connect_status;

typedef enum {
    SC_DB_BUSY, SC_DB_ROWS, SC_DB_TUPLES, SC_DB_COMMAND, SC_DB_ERROR, SC_DB_COPY,
    SC_DB_END, SC_DB_LOST
} sc_db_next_kind;

typedef enum { SC_DB_QUERY, SC_DB_META } sc_db_role;
typedef enum {
    SC_DB_START_OK, SC_DB_START_MALFORMED, SC_DB_START_TOO_MANY, SC_DB_START_FAILED
} sc_db_start;
typedef enum { SC_DB_CANCEL_SENT, SC_DB_CANCEL_PENDING, SC_DB_CANCEL_FAILED } sc_db_cancel_status;

#define SC_DB_CELL_NULL SC_PQ_CELL_NULL
#define SC_DB_CELL_TRUNCATED SC_PQ_CELL_TRUNCATED
#define SC_DB_WAIT_SOCK SC_PQ_WAIT_SOCK
#define SC_DB_WAIT_WAKE SC_PQ_WAIT_WAKE
#define SC_DB_WAIT_CANCEL SC_PQ_WAIT_CANCEL
#define SC_DB_CANCEL_DEADLINE_MS SC_PQ_CANCEL_DEADLINE_MS

typedef enum {
    SC_DB_FAIL_NETWORK, SC_DB_FAIL_AUTH, SC_DB_FAIL_PASSWORD, SC_DB_FAIL_LOCAL
} sc_db_fail_kind;

sc_result_size sc_db_connect_start(sc_db *c, sc_db_engine engine, const sc_arena *a,
                                   sc_str target, sc_str password, sc_db_role role);
void sc_db_control_init(sc_db_control *control);
void sc_db_control_arm(sc_db_control *control, uint64_t request);
void sc_db_control_cancel(sc_db_control *control, uint64_t request);
void sc_db_control_close(sc_db_control *control);
bool sc_db_control_cancelled(sc_db_control *control);
void sc_db_set_control(sc_db c, sc_db_control *control);
bool sc_db_is_local(sc_db c);
bool sc_db_label_columns(sc_db c);
bool sc_db_next_blocks(sc_db c);
bool sc_db_put_path(sc_db c, const sc_arena *a, sc_buf *b);
bool sc_db_put_connected(sc_db c, const sc_arena *a, sc_buf *b);
bool sc_db_put_start_failed(sc_db c, sc_db_start start, const sc_arena *a, sc_buf *b);
sc_db_fail_kind sc_db_fail_from_status(sc_db c, sc_db_connect_status st);
bool sc_db_put_unsent_code(sc_db c, const sc_arena *a, sc_buf *b);
sc_db_start sc_db_start_reason(sc_result_size r);
sc_db_connect_status sc_db_connect_poll(sc_db c, bool ready, bool *want_write);
sc_result_size sc_db_wait(sc_db c, sc_wake w, bool want_write, uint32_t timeout_ms);
sc_result_size sc_db_send_query(sc_db c, const sc_arena *a, sc_str sql);
bool sc_db_idle_alive(sc_db c);
bool sc_db_flush_pending(sc_db c);
sc_db_next_kind sc_db_next(sc_db c);
size_t sc_db_rows(sc_db c);
size_t sc_db_columns(sc_db c);
bool sc_db_put_columns(sc_db c, const sc_arena *a, sc_buf *b);
bool sc_db_put_row(sc_db c, size_t row, size_t cap, const sc_arena *a, sc_buf *b);
bool sc_db_row_failed(sc_db c);
bool sc_db_put_tag(sc_db c, const sc_arena *a, sc_buf *b);
bool sc_db_put_error(sc_db c, const sc_arena *a, sc_buf *b);
bool sc_db_put_error_message(sc_db c, const sc_arena *a, sc_buf *b);
uint64_t sc_db_transaction(sc_db c);
sc_db_cancel_status sc_db_cancel_start(sc_db c);
sc_db_cancel_status sc_db_cancel_poll(sc_db c, bool ready);
void sc_db_finish(sc_db *c);

#endif
