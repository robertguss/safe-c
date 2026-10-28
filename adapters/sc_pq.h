#ifndef SAFEC_ADAPTER_PQ_H
#define SAFEC_ADAPTER_PQ_H

#include "safec/buf.h"
#include "safec/result.h"
#include "sc_thread.h"

/* The libpq adapter (adapters/pq.c, subset rule R0), for the explorer's worker
 * threads (plan U7, KTD7). A connection is an opaque slot index; every call on
 * one connection comes from the one thread that started it. Nothing here
 * blocks except sc_pq_wait (up to its timeout) and, before libpq 17,
 * sc_pq_cancel_start, which sends the cancel request on a connection of its
 * own.
 *
 * Results are read one at a time with sc_pq_next. The adapter keeps the
 * current result until the next call or sc_pq_finish, and writes its column
 * names, rows, command tag and error fields straight into a caller's sc_buf, in
 * the field format examples/explorer/proto.h reads: a u64 is 8 little-endian
 * bytes, and a string is a u64 length and then its bytes. Each write is whole
 * or nothing: when it does not fit, the buffer is as it was and the call
 * returns false. */

/* A cell is a u64 meta word, then a string of at most the caller's cap bytes.
 * The meta word holds the value's true length in its low bits. */
#define SC_PQ_CELL_NULL (UINT64_C(1) << 62)
#define SC_PQ_CELL_TRUNCATED (UINT64_C(1) << 63)

typedef struct {
    size_t slot;
    bool live;
} sc_pq;

typedef enum {
    SC_PQ_CONNECTING, /* still connecting: wait on the socket and poll again */
    SC_PQ_CONNECTED,
    SC_PQ_FAILED_AUTH,    /* the server wanted a password, or refused the one given */
    SC_PQ_NEEDS_PASSWORD,
    SC_PQ_FAILED_NETWORK  /* every other failure */
} sc_pq_connect_status;

typedef enum {
    SC_PQ_BUSY,    /* nothing ready: wait on the socket */
    SC_PQ_ROWS,    /* one row of the current statement (single-row mode) */
    SC_PQ_TUPLES,  /* the statement's rows ended; it has columns and a tag */
    SC_PQ_COMMAND, /* a statement with no rows completed; it has a tag */
    SC_PQ_ERROR,   /* the statement failed; it has error fields */
    SC_PQ_COPY,    /* a COPY started; the adapter ends or drains it, and a result follows */
    SC_PQ_END,     /* the query has no more results */
    SC_PQ_LOST     /* the connection is gone */
} sc_pq_next_kind;

/* Which of the explorer's connections this is (plan KTD21). */
typedef enum {
    SC_PQ_QUERY, /* the user's queries: application_name=explorer */
    SC_PQ_META   /* catalog reads: application_name=explorer-meta */
} sc_pq_role;

/* Starts connecting with `conninfo`, a libpq connection string or URI; an empty
 * one uses the PG* environment and ~/.pgpass. The adapter adds
 * client_encoding=UTF8, the role's application_name and TCP keepalives
 * (keepalives_idle=10, keepalives_interval=5, keepalives_count=3, so a dropped
 * network ends in a lost connection within about 25 s), which the conninfo
 * cannot override; SQL can still change client_encoding, so cell bytes are
 * not guaranteed UTF-8. Server options come from libpq alone (the conninfo,
 * a service file or PGOPTIONS) for either role; the metadata link sets its
 * statement_timeout in SQL (catalog.h). Server notices are dropped. Fails with EINVAL for a string with a NUL byte, not
 * live in `a`, or that libpq cannot parse (its message would quote the
 * string, password and all), ENOMEM when libpq cannot start, and EMFILE when
 * every slot is taken. On success *c is live. */
sc_result_size sc_pq_connect_start(sc_pq *c, const sc_arena *a, sc_str conninfo, sc_str password,
                                  sc_pq_role role);

/* Why sc_pq_connect_start failed, for a fixed message: libpq has none, or one
 * that must not be shown. */
typedef enum {
    SC_PQ_START_OK,
    SC_PQ_START_MALFORMED, /* a NUL byte, or libpq cannot parse it */
    SC_PQ_START_TOO_MANY,  /* every slot is taken */
    SC_PQ_START_FAILED     /* libpq could not start */
} sc_pq_start;
sc_pq_start sc_pq_start_reason(sc_result_size r);

/* Advances the connect when `ready`, which means sc_pq_wait reported the
 * socket ready for what `want_write` asked: libpq blocks inside
 * PQconnectPoll's send if it is called earlier. Not ready, it only checks the
 * deadline. It fails as a network failure once the conninfo's
 * connect_timeout (default 10 s) passes, which libpq checks only inside its
 * blocking connect. After a failure the connection's error message is
 * kept for sc_pq_put_error_message until sc_pq_finish. `want_write` says what
 * to wait for before the next poll. */
sc_pq_connect_status sc_pq_connect_poll(sc_pq c, bool ready, bool *want_write);

/* Waits until the socket is ready (readable, or writable when `want_write`),
 * the wake rings, a pending cancel's own socket is ready, or `timeout_ms`
 * passes. The value has bit 1 for the socket, bit 2 for the wake and bit 4
 * for the cancel's socket; the worker passes bit 1 to sc_pq_connect_poll and
 * sc_pq_idle_alive, and bit 4 to sc_pq_cancel_poll. Rings stay pending: drain
 * the wake afterwards. */
sc_result_size sc_pq_wait(sc_pq c, sc_wake w, bool want_write, uint32_t timeout_ms);

/* Sends `sql` with the simple query protocol (KTD12) and asks for rows one at a
 * time (single-row mode on every libpq). Fails with EINVAL for bytes not
 * live in `a` or with a NUL, and EIO when libpq refuses (the connection is
 * gone). */
sc_result_size sc_pq_send_query(sc_pq c, const sc_arena *a, sc_str sql);

/* With no query running: reads whatever the socket has (a notice, a notify, a
 * FATAL before the server hangs up) and says whether the connection is still
 * up. Call it when the socket is readable, so an idle connection that dropped
 * is noticed and a readable socket does not spin the wait. */
bool sc_pq_idle_alive(sc_pq c);

/* Whether the query is still being written to the socket; call again when the
 * socket is writable. False also when the connection is gone, which the next
 * sc_pq_next reports. */
bool sc_pq_flush_pending(sc_pq c);

/* Reads what the socket has and returns the next result's kind. SC_PQ_ERROR
 * with a FATAL or PANIC severity, which ends the session, comes back as
 * SC_PQ_LOST, as does a closed socket. */
sc_pq_next_kind sc_pq_next(sc_pq c);

/* The current result's rows and columns. */
size_t sc_pq_rows(sc_pq c);
size_t sc_pq_columns(sc_pq c);

/* Appends the current result's column count, then each column's name and type
 * oid. */
bool sc_pq_put_columns(sc_pq c, const sc_arena *a, sc_buf *b);

/* Appends one cell per column of row `row` of the current result, each value
 * cut to `cap` bytes. */
bool sc_pq_put_row(sc_pq c, size_t row, size_t cap, const sc_arena *a, sc_buf *b);

/* Appends the current result's command tag ("SELECT 3", "INSERT 0 1"). */
bool sc_pq_put_tag(sc_pq c, const sc_arena *a, sc_buf *b);

/* Appends the current error's SQLSTATE, message, detail and hint as strings
 * (each cut to 16 KiB),
 * then its 1-based character position as a u64 (0 when it has none). */
bool sc_pq_put_error(sc_pq c, const sc_arena *a, sc_buf *b);

/* Appends the connection's error message (cut to 16 KiB), without trailing
 * newlines and spaces, or "timeout expired" after a connect timeout. */
bool sc_pq_put_error_message(sc_pq c, const sc_arena *a, sc_buf *b);

/* Appends, as a string, the value libpq holds on the connection for conninfo
 * option `key` (live in `ka`), such as "keepalives_idle". False when libpq
 * has no such option or it is unset. */
bool sc_pq_put_option(sc_pq c, const sc_arena *ka, sc_str key, const sc_arena *a, sc_buf *b);

/* The transaction status: 0 idle, 1 active, 2 in a transaction, 3 in a failed
 * transaction, 4 unknown. */
uint64_t sc_pq_transaction(sc_pq c);

/* Asking the server to cancel the running query (plan KTD7). The query then
 * ends with an error result, or completes if the cancel came too late. */
typedef enum {
    SC_PQ_CANCEL_SENT,    /* the server has the cancel request */
    SC_PQ_CANCEL_PENDING, /* under way: wait (bit 4) and poll */
    SC_PQ_CANCEL_FAILED   /* it could not be sent, or passed its deadline */
} sc_pq_cancel_status;

/* The deadline of a non-blocking cancel, which connect_timeout does not cover.
 * A test build may set a shorter one. */
#ifndef SC_PQ_CANCEL_DEADLINE_MS
#define SC_PQ_CANCEL_DEADLINE_MS 10000u
#endif

/* Starts a cancel. With libpq 17 and later it is the encrypted, non-blocking
 * PQcancelStart, PENDING until sc_pq_cancel_poll ends it; before 17 it is the
 * blocking PQcancel, SENT or FAILED at once. A cancel already pending is
 * kept. */
sc_pq_cancel_status sc_pq_cancel_start(sc_pq c);

/* Advances a pending cancel: `ready` means sc_pq_wait reported its socket.
 * FAILED once SC_PQ_CANCEL_DEADLINE_MS have passed since the start. SENT when
 * none is pending. */
sc_pq_cancel_status sc_pq_cancel_poll(sc_pq c, bool ready);

/* Closes the connection, and any pending cancel, and frees its slot; *c is
 * no longer live. */
void sc_pq_finish(sc_pq *c);

#endif
