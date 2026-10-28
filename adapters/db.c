#include "sc_db.h"
#include <errno.h>
#if defined(SC_DB_DUCKDB_ENABLED) || defined(SC_DB_SQLITE_ENABLED)
#define SC_DB_EMBEDDED 1
#endif
#ifdef SC_DB_EMBEDDED
#include "sc_local.h"
#include <string.h>

static sc_local *local(sc_db c) {
    return (sc_local *)c.local;
}
#endif

void sc_db_control_init(sc_db_control *control) {
    atomic_init(&control->request, 0);
    atomic_init(&control->cancelled, 0);
    atomic_init(&control->closing, false);
}

void sc_db_control_arm(sc_db_control *control, uint64_t request) {
    atomic_store(&control->cancelled, 0);
    atomic_store(&control->request, request);
}

void sc_db_control_cancel(sc_db_control *control, uint64_t request) {
    if (request != 0 && atomic_load(&control->request) == request)
        atomic_store(&control->cancelled, request);
}

void sc_db_control_close(sc_db_control *control) {
    atomic_store(&control->closing, true);
}

bool sc_db_control_cancelled(sc_db_control *control) {
    uint64_t request = atomic_load(&control->request);
    return atomic_load(&control->closing) ||
           (request != 0 && atomic_load(&control->cancelled) == request);
}

void sc_db_set_control(sc_db c, sc_db_control *control) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL && c.live) local(c)->control = control;
#else
    (void)c;
    (void)control;
#endif
}

bool sc_db_put_path(sc_db c, const sc_arena *a, sc_buf *b) {
#ifdef SC_DB_EMBEDDED
    sc_local *s = local(c);
    if (c.engine != SC_DB_POSTGRESQL && s != NULL && s->path != NULL)
        return sc_local_string(a, b, s->path, strlen(s->path));
#else
    (void)c;
    (void)a;
    (void)b;
#endif
    return false;
}

sc_result_size sc_db_connect_start(sc_db *c, sc_db_engine engine, const sc_arena *a,
                                   sc_str target, sc_str password, sc_db_role role) {
    c->engine = engine;
#ifdef SC_DB_DUCKDB_ENABLED
    if (engine == SC_DB_DUCKDB) return sc_duckdb_open(c, a, target);
#endif
#ifdef SC_DB_SQLITE_ENABLED
    if (engine == SC_DB_SQLITE) return sc_sqlite_open(c, a, target);
#endif
    if (engine != SC_DB_POSTGRESQL)
        return sc_err_size(EINVAL);
    sc_result_size r = sc_pq_connect_start(&c->pg, a, target, password, (sc_pq_role)role);
    c->engine = engine;
    c->live = c->pg.live;
    return r;
}

sc_db_start sc_db_start_reason(sc_result_size r) {
    return (sc_db_start)sc_pq_start_reason(r);
}

sc_db_connect_status sc_db_connect_poll(sc_db c, bool ready, bool *want_write) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL) {
        *want_write = false;
        return local(c)->ready ? SC_DB_CONNECTED : SC_DB_FAILED_NETWORK;
    }
#endif
    return (sc_db_connect_status)sc_pq_connect_poll(c.pg, ready, want_write);
}

sc_result_size sc_db_wait(sc_db c, sc_wake w, bool want_write, uint32_t timeout_ms) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL) {
        if (!local(c)->ended) return sc_ok_size(SC_DB_WAIT_SOCK);
        sc_result_size r = sc_wake_wait(w, timeout_ms);
        return r.ok ? sc_ok_size(r.value ? SC_DB_WAIT_WAKE : 0) : r;
    }
#endif
    return sc_pq_wait(c.pg, w, want_write, timeout_ms);
}

sc_result_size sc_db_send_query(sc_db c, const sc_arena *a, sc_str sql) {
#ifdef SC_DB_DUCKDB_ENABLED
    if (c.engine == SC_DB_DUCKDB) return sc_duckdb_send(local(c), a, sql);
#endif
#ifdef SC_DB_SQLITE_ENABLED
    if (c.engine == SC_DB_SQLITE) return sc_sqlite_send(local(c), a, sql);
#endif
    return sc_pq_send_query(c.pg, a, sql);
}

bool sc_db_idle_alive(sc_db c) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL) return local(c)->ready;
#endif
    return sc_pq_idle_alive(c.pg);
}

bool sc_db_flush_pending(sc_db c) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL) return false;
#endif
    return sc_pq_flush_pending(c.pg);
}

sc_db_next_kind sc_db_next(sc_db c) {
#ifdef SC_DB_DUCKDB_ENABLED
    if (c.engine == SC_DB_DUCKDB) return sc_duckdb_next(local(c));
#endif
#ifdef SC_DB_SQLITE_ENABLED
    if (c.engine == SC_DB_SQLITE) return sc_sqlite_next(local(c));
#endif
    return (sc_db_next_kind)sc_pq_next(c.pg);
}

size_t sc_db_rows(sc_db c) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL) return local(c)->rows;
#endif
    return sc_pq_rows(c.pg);
}

size_t sc_db_columns(sc_db c) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL) return local(c)->cols;
#endif
    return sc_pq_columns(c.pg);
}

bool sc_db_put_columns(sc_db c, const sc_arena *a, sc_buf *b) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL) return sc_local_columns(local(c), a, b);
#endif
    return sc_pq_put_columns(c.pg, a, b);
}

bool sc_db_put_row(sc_db c, size_t row, size_t cap, const sc_arena *a, sc_buf *b) {
#ifdef SC_DB_DUCKDB_ENABLED
    if (c.engine == SC_DB_DUCKDB) return sc_duckdb_row(local(c), row, cap, a, b);
#endif
#ifdef SC_DB_SQLITE_ENABLED
    if (c.engine == SC_DB_SQLITE) return sc_sqlite_row(local(c), row, cap, a, b);
#endif
    return sc_pq_put_row(c.pg, row, cap, a, b);
}

bool sc_db_row_failed(sc_db c) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL) return local(c)->row_error;
#else
    (void)c;
#endif
    return false;
}

bool sc_db_put_tag(sc_db c, const sc_arena *a, sc_buf *b) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL) return sc_local_string(a, b, local(c)->tag, strlen(local(c)->tag));
#endif
    return sc_pq_put_tag(c.pg, a, b);
}

bool sc_db_put_error(sc_db c, const sc_arena *a, sc_buf *b) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL) return sc_local_put_error(local(c), a, b);
#endif
    return sc_pq_put_error(c.pg, a, b);
}

bool sc_db_put_error_message(sc_db c, const sc_arena *a, sc_buf *b) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL) return sc_local_string(a, b, local(c)->error, strlen(local(c)->error));
#endif
    return sc_pq_put_error_message(c.pg, a, b);
}

uint64_t sc_db_transaction(sc_db c) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL) return local(c)->transaction;
#endif
    return sc_pq_transaction(c.pg);
}

sc_db_cancel_status sc_db_cancel_start(sc_db c) {
#ifdef SC_DB_DUCKDB_ENABLED
    if (c.engine == SC_DB_DUCKDB) return sc_duckdb_cancel(local(c));
#endif
#ifdef SC_DB_SQLITE_ENABLED
    if (c.engine == SC_DB_SQLITE) return sc_sqlite_cancel(local(c));
#endif
    return (sc_db_cancel_status)sc_pq_cancel_start(c.pg);
}

sc_db_cancel_status sc_db_cancel_poll(sc_db c, bool ready) {
#ifdef SC_DB_EMBEDDED
    if (c.engine != SC_DB_POSTGRESQL) return SC_DB_CANCEL_SENT;
#endif
    return (sc_db_cancel_status)sc_pq_cancel_poll(c.pg, ready);
}

void sc_db_finish(sc_db *c) {
#ifdef SC_DB_DUCKDB_ENABLED
    if (c->engine == SC_DB_DUCKDB && c->live) {
        sc_duckdb_finish(local(*c));
        c->local = 0;
        c->live = false;
        return;
    }
#endif
#ifdef SC_DB_SQLITE_ENABLED
    if (c->engine == SC_DB_SQLITE && c->live) {
        sc_sqlite_finish(local(*c));
        c->local = 0;
        c->live = false;
        return;
    }
#endif
    sc_pq_finish(&c->pg);
    c->live = false;
}
