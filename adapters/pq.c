/* The libpq adapter (subset rule R0). It holds every PGconn and PGresult, so
 * application code sees slot indices and copies of the bytes. Unsafe C stays
 * here: C strings from libpq, raw descriptors for poll, and the byte copies. */
#define _POSIX_C_SOURCE 200809L
#include "sc_pq.h"
#include <errno.h>
#include <libpq-fe.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SLOTS 64
#define CONNECT_TIMEOUT_MS 10000u
#define ERROR_FIELD_CAP 16384u /* each error field, so a reply always fits */

typedef struct {
    PGconn *conn;
    PGresult *res;
    bool used;
    bool copy_out; /* draining a COPY TO STDOUT */
    uint64_t deadline_ms; /* the connect gives up then (libpq checks connect_timeout only in PQconnectdb) */
    bool timed_out;
} slot;

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* The conninfo's connect_timeout in ms, or the default. */
static uint64_t connect_timeout_ms(PGconn *conn) {
    uint64_t ms = CONNECT_TIMEOUT_MS;
    PQconninfoOption *opts = PQconninfo(conn);
    for (PQconninfoOption *o = opts; o != NULL && o->keyword != NULL; o++) {
        if (strcmp(o->keyword, "connect_timeout") == 0 && o->val != NULL && o->val[0] != 0) {
            long v = strtol(o->val, NULL, 10);
            if (v > 0)
                ms = (uint64_t)(v < 2 ? 2 : v) * 1000u; /* libpq's own minimum is 2 s */
        }
    }
    PQconninfoFree(opts);
    return ms;
}

static void quiet(void *arg, const char *message) {
    (void)arg;
    (void)message; /* NOTICEs would go to stderr; the explorer drops them */
}

static slot slots[SLOTS];
static pthread_mutex_t slots_lock = PTHREAD_MUTEX_INITIALIZER;

static slot *at(sc_pq c) {
    return c.live && c.slot < SLOTS ? &slots[c.slot] : NULL;
}

/* A copy of `s` as a C string, or NULL for bytes that are not live or hold a
 * NUL. The caller frees it. An empty string is "". */
static char *to_cstr(const sc_arena *a, sc_str s) {
    sc_slice live = s.len == 0 ? s : sc_slice_sub(a, s, 0, s.len);
    if (s.len > 0 && live.ptr == NULL)
        return NULL;
    char *out = malloc(s.len + 1);
    if (out == NULL)
        return NULL;
    for (size_t i = 0; i < s.len; i++) {
        if (live.ptr[i] == 0) {
            free(out);
            return NULL;
        }
        out[i] = (char)live.ptr[i];
    }
    out[s.len] = 0;
    return out;
}

/* Appends without leaving a partial write: on failure *b is restored. */
static bool put_bytes(const sc_arena *a, sc_buf *b, const void *p, size_t n) {
    return n == 0 || sc_buf_lit(a, b, (const uint8_t *)p, n);
}

static bool put_u64(const sc_arena *a, sc_buf *b, uint64_t v) {
    uint8_t w[8];
    for (unsigned i = 0; i < 8; i++)
        w[i] = (uint8_t)(v >> (8 * i));
    return put_bytes(a, b, w, 8);
}

static bool put_str(const sc_arena *a, sc_buf *b, const char *s, size_t n) {
    return put_u64(a, b, n) && put_bytes(a, b, s, n);
}

static bool put_cstr(const sc_arena *a, sc_buf *b, const char *s) {
    return put_str(a, b, s == NULL ? "" : s, s == NULL ? 0 : strlen(s));
}

/* At most `cap` bytes of `s`, so a hostile message cannot fill a reply. */
static bool put_cstr_cap(const sc_arena *a, sc_buf *b, const char *s, size_t cap) {
    size_t n = s == NULL ? 0 : strlen(s);
    return put_str(a, b, s == NULL ? "" : s, n < cap ? n : cap);
}

/* Runs `ok` and restores *b when it fails, so a write is whole or nothing. */
#define WHOLE(b, ok)                                                                               \
    do {                                                                                           \
        sc_buf keep_ = *(b);                                                                       \
        if (!(ok)) {                                                                               \
            *(b) = keep_;                                                                          \
            return false;                                                                          \
        }                                                                                          \
        return true;                                                                               \
    } while (0)

sc_result_size sc_pq_connect_start(sc_pq *c, const sc_arena *a, sc_str conninfo) {
    c->live = false;
    char *info = to_cstr(a, conninfo);
    if (info == NULL)
        return sc_err_size(EINVAL);
    const char *keys[] = {"dbname", "client_encoding", "application_name", NULL};
    const char *values[] = {info, "UTF8", "explorer", NULL};
    PGconn *conn = PQconnectStartParams(keys, values, 1);
    free(info);
    if (conn == NULL)
        return sc_err_size(ENOMEM);
    pthread_mutex_lock(&slots_lock);
    size_t i = 0;
    while (i < SLOTS && slots[i].used)
        i++;
    if (i < SLOTS) {
        slots[i].used = true;
        slots[i].conn = conn;
        slots[i].res = NULL;
        slots[i].copy_out = false;
        slots[i].timed_out = false;
        slots[i].deadline_ms = now_ms() + connect_timeout_ms(conn);
        PQsetNoticeProcessor(conn, quiet, NULL);
    }
    pthread_mutex_unlock(&slots_lock);
    if (i == SLOTS) {
        PQfinish(conn);
        return sc_err_size(EMFILE);
    }
    c->slot = i;
    c->live = true;
    return sc_ok_size(0);
}

static bool auth_failure(PGconn *conn) {
    /* No password and the server asked for one, or one was sent and refused:
     * libpq has no SQLSTATE for a failed connect, so 28P01 shows only here. */
    if (PQconnectionNeedsPassword(conn))
        return true;
    const char *msg = PQerrorMessage(conn);
    return msg != NULL && (strstr(msg, "password authentication failed") != NULL ||
                           strstr(msg, "28P01") != NULL);
}

sc_pq_connect_status sc_pq_connect_poll(sc_pq c, bool ready, bool *want_write) {
    slot *s = at(c);
    if (s == NULL)
        return SC_PQ_FAILED_NETWORK;
    if (PQstatus(s->conn) == CONNECTION_BAD)
        return auth_failure(s->conn) ? SC_PQ_FAILED_AUTH : SC_PQ_FAILED_NETWORK;
    if (now_ms() >= s->deadline_ms) {
        s->timed_out = true;
        return SC_PQ_FAILED_NETWORK;
    }
    if (!ready)
        return SC_PQ_CONNECTING; /* PQconnectPoll would block in a send */
    *want_write = false;
    switch (PQconnectPoll(s->conn)) {
    case PGRES_POLLING_OK:
        if (PQsetnonblocking(s->conn, 1) != 0)
            return SC_PQ_FAILED_NETWORK;
        return SC_PQ_CONNECTED;
    case PGRES_POLLING_WRITING:
        *want_write = true;
        return SC_PQ_CONNECTING;
    case PGRES_POLLING_READING:
        return SC_PQ_CONNECTING;
    default:
        return auth_failure(s->conn) ? SC_PQ_FAILED_AUTH : SC_PQ_FAILED_NETWORK;
    }
}

sc_result_size sc_pq_wait(sc_pq c, sc_wake w, bool want_write, uint32_t timeout_ms) {
    slot *s = at(c);
    struct pollfd fds[2];
    nfds_t n = 0;
    int sock = s == NULL ? -1 : PQsocket(s->conn);
    if (sock >= 0) {
        fds[n].fd = sock;
        fds[n].events = (short)(want_write ? POLLOUT : POLLIN);
        fds[n].revents = 0;
        n++;
    }
    fds[n].fd = sc_wake_fd(w);
    fds[n].events = POLLIN;
    fds[n].revents = 0;
    n++;
    int timeout = timeout_ms > (uint32_t)INT32_MAX ? INT32_MAX : (int)timeout_ms;
    int got = poll(fds, n, timeout);
    if (got < 0)
        return errno == EINTR ? sc_ok_size(0) : sc_err_size(errno);
    size_t bits = 0;
    if (sock >= 0 && fds[0].revents != 0)
        bits |= 1;
    if (fds[n - 1].revents != 0)
        bits |= 2;
    return sc_ok_size(bits);
}

sc_result_size sc_pq_send_query(sc_pq c, const sc_arena *a, sc_str sql) {
    slot *s = at(c);
    if (s == NULL)
        return sc_err_size(EINVAL);
    char *q = to_cstr(a, sql);
    if (q == NULL)
        return sc_err_size(EINVAL);
    int sent = PQsendQuery(s->conn, q);
    free(q);
    if (sent != 1)
        return sc_err_size(EIO);
#ifdef LIBPQ_HAS_CHUNK_MODE
    (void)PQsetChunkedRowsMode(s->conn, 256);
#else
    (void)PQsetSingleRowMode(s->conn);
#endif
    return sc_ok_size(0);
}

bool sc_pq_idle_alive(sc_pq c) {
    slot *s = at(c);
    if (s == NULL || PQconsumeInput(s->conn) != 1 || PQstatus(s->conn) == CONNECTION_BAD)
        return false;
    PGnotify *n;
    while ((n = PQnotifies(s->conn)) != NULL)
        PQfreemem(n);
    return true;
}

bool sc_pq_flush_pending(sc_pq c) {
    slot *s = at(c);
    return s != NULL && PQflush(s->conn) == 1;
}

static void clear(slot *s) {
    if (s->res != NULL) {
        PQclear(s->res);
        s->res = NULL;
    }
}

/* Drains COPY TO STDOUT data. True when it has ended. */
static bool drain_copy(slot *s) {
    for (;;) {
        char *row = NULL;
        int n = PQgetCopyData(s->conn, &row, 1);
        if (row != NULL)
            PQfreemem(row);
        if (n == 0)
            return false;
        if (n < 0) {
            s->copy_out = false;
            return true;
        }
    }
}

sc_pq_next_kind sc_pq_next(sc_pq c) {
    slot *s = at(c);
    if (s == NULL)
        return SC_PQ_LOST;
    clear(s);
    if (PQconsumeInput(s->conn) != 1 || PQstatus(s->conn) == CONNECTION_BAD)
        return SC_PQ_LOST;
    if (s->copy_out && !drain_copy(s))
        return SC_PQ_BUSY;
    if (PQisBusy(s->conn))
        return SC_PQ_BUSY;
    s->res = PQgetResult(s->conn);
    if (s->res == NULL)
        return SC_PQ_END;
    switch (PQresultStatus(s->res)) {
    case PGRES_SINGLE_TUPLE:
#ifdef LIBPQ_HAS_CHUNK_MODE
    case PGRES_TUPLES_CHUNK:
#endif
        return SC_PQ_ROWS;
    case PGRES_TUPLES_OK:
        return SC_PQ_TUPLES;
    case PGRES_COMMAND_OK:
    case PGRES_EMPTY_QUERY:
        return SC_PQ_COMMAND;
    case PGRES_COPY_IN:
        /* Blocking for this one message: in nonblocking mode PQputCopyEnd may
         * queue nothing, and the server would wait for copy data. */
        (void)PQsetnonblocking(s->conn, 0);
        (void)PQputCopyEnd(s->conn, "COPY is not supported");
        (void)PQflush(s->conn);
        (void)PQsetnonblocking(s->conn, 1);
        return SC_PQ_COPY;
    case PGRES_COPY_OUT:
        s->copy_out = true;
        return SC_PQ_COPY;
    default: {
        const char *sev = PQresultErrorField(s->res, PG_DIAG_SEVERITY_NONLOCALIZED);
        if (sev != NULL && (strcmp(sev, "FATAL") == 0 || strcmp(sev, "PANIC") == 0))
            return SC_PQ_LOST;
        if (PQstatus(s->conn) == CONNECTION_BAD)
            return SC_PQ_LOST;
        return SC_PQ_ERROR;
    }
    }
}

size_t sc_pq_rows(sc_pq c) {
    slot *s = at(c);
    return s == NULL || s->res == NULL ? 0 : (size_t)PQntuples(s->res);
}

size_t sc_pq_columns(sc_pq c) {
    slot *s = at(c);
    return s == NULL || s->res == NULL ? 0 : (size_t)PQnfields(s->res);
}

static bool columns_into(slot *s, const sc_arena *a, sc_buf *b) {
    int n = PQnfields(s->res);
    if (!put_u64(a, b, (uint64_t)n))
        return false;
    for (int i = 0; i < n; i++) {
        if (!put_cstr(a, b, PQfname(s->res, i)) || !put_u64(a, b, (uint64_t)PQftype(s->res, i)))
            return false;
    }
    return true;
}

bool sc_pq_put_columns(sc_pq c, const sc_arena *a, sc_buf *b) {
    slot *s = at(c);
    if (s == NULL || s->res == NULL)
        return false;
    WHOLE(b, columns_into(s, a, b));
}

static bool row_into(slot *s, int row, size_t cap, const sc_arena *a, sc_buf *b) {
    int n = PQnfields(s->res);
    for (int i = 0; i < n; i++) {
        uint64_t meta = 0;
        size_t len = 0, keep = 0;
        if (PQgetisnull(s->res, row, i)) {
            meta = SC_PQ_CELL_NULL;
        } else {
            len = (size_t)PQgetlength(s->res, row, i);
            keep = len < cap ? len : cap;
            meta = (uint64_t)len | (keep < len ? SC_PQ_CELL_TRUNCATED : 0);
        }
        if (!put_u64(a, b, meta) || !put_str(a, b, PQgetvalue(s->res, row, i), keep))
            return false;
    }
    return true;
}

bool sc_pq_put_row(sc_pq c, size_t row, size_t cap, const sc_arena *a, sc_buf *b) {
    slot *s = at(c);
    if (s == NULL || s->res == NULL || row >= (size_t)PQntuples(s->res))
        return false;
    WHOLE(b, row_into(s, (int)row, cap, a, b));
}

bool sc_pq_put_tag(sc_pq c, const sc_arena *a, sc_buf *b) {
    slot *s = at(c);
    if (s == NULL || s->res == NULL)
        return false;
    WHOLE(b, put_cstr(a, b, PQcmdStatus(s->res)));
}

static bool error_into(slot *s, const sc_arena *a, sc_buf *b) {
    const char *pos = PQresultErrorField(s->res, PG_DIAG_STATEMENT_POSITION);
    uint64_t p = pos == NULL ? 0 : strtoull(pos, NULL, 10);
    return put_cstr_cap(a, b, PQresultErrorField(s->res, PG_DIAG_SQLSTATE), 5) &&
           put_cstr_cap(a, b, PQresultErrorField(s->res, PG_DIAG_MESSAGE_PRIMARY), ERROR_FIELD_CAP) &&
           put_cstr_cap(a, b, PQresultErrorField(s->res, PG_DIAG_MESSAGE_DETAIL), ERROR_FIELD_CAP) &&
           put_cstr_cap(a, b, PQresultErrorField(s->res, PG_DIAG_MESSAGE_HINT), ERROR_FIELD_CAP) &&
           put_u64(a, b, p);
}

bool sc_pq_put_error(sc_pq c, const sc_arena *a, sc_buf *b) {
    slot *s = at(c);
    if (s == NULL || s->res == NULL)
        return false;
    WHOLE(b, error_into(s, a, b));
}

bool sc_pq_put_error_message(sc_pq c, const sc_arena *a, sc_buf *b) {
    slot *s = at(c);
    const char *msg = s == NULL ? "" : s->timed_out ? "timeout expired" : PQerrorMessage(s->conn);
    size_t n = strlen(msg);
    n = n < ERROR_FIELD_CAP ? n : ERROR_FIELD_CAP;
    while (n > 0 && (msg[n - 1] == '\n' || msg[n - 1] == ' '))
        n--;
    WHOLE(b, put_str(a, b, msg, n));
}

uint64_t sc_pq_transaction(sc_pq c) {
    slot *s = at(c);
    if (s == NULL)
        return 4;
    switch (PQtransactionStatus(s->conn)) {
    case PQTRANS_IDLE:
        return 0;
    case PQTRANS_ACTIVE:
        return 1;
    case PQTRANS_INTRANS:
        return 2;
    case PQTRANS_INERROR:
        return 3;
    default:
        return 4;
    }
}

bool sc_pq_cancel(sc_pq c) {
    slot *s = at(c);
    if (s == NULL)
        return false;
#ifdef LIBPQ_HAS_ASYNC_CANCEL
    /* The encrypted cancel; blocking here, on the worker's own thread. */
    PGcancelConn *cc = PQcancelCreate(s->conn);
    if (cc == NULL)
        return false;
    bool ok = PQcancelBlocking(cc) == 1;
    PQcancelFinish(cc);
    return ok;
#else
    PGcancel *cancel = PQgetCancel(s->conn);
    if (cancel == NULL)
        return false;
    char err[256];
    bool ok = PQcancel(cancel, err, (int)sizeof err) == 1;
    PQfreeCancel(cancel);
    return ok;
#endif
}

void sc_pq_finish(sc_pq *c) {
    slot *s = at(*c);
    c->live = false;
    if (s == NULL)
        return;
    clear(s);
    PQfinish(s->conn);
    pthread_mutex_lock(&slots_lock);
    s->conn = NULL;
    s->copy_out = false;
    s->used = false;
    pthread_mutex_unlock(&slots_lock);
}
