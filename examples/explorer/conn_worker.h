#ifndef EXPLORER_CONN_WORKER_H
#define EXPLORER_CONN_WORKER_H

/* The worker side of the connection machines (conn.h): the conn_worker
 * machine, a step function from (state, event) to the actions worker.c
 * performs. Core code: it includes only conn.h. */

#include "conn.h"

typedef enum {
    W_IDLE,       /* no connection yet */
    W_CONNECTING,
    W_READY,      /* connected, no query */
    W_RUNNING,
    W_CANCELLING, /* a cancel or cap is in progress; results drain to the end */
    W_DRAINING,   /* an error came; results drain to the end */
    W_CLOSING,    /* the connection is gone; waits for CLOSE */
    W_CLOSED      /* BYE sent or pending; the loop ends once it is sent */
} worker_state;

typedef enum {
    W_EV_MSG,       /* a message from the UI: header and arena */
    W_EV_CONNECTED, /* libpq: the connection is up */
    W_EV_CONN_FAILED, /* libpq: it failed; aux: PROTO_FAIL_* */
    W_EV_COLUMNS,   /* libpq: a result set starts */
    W_EV_ROW,       /* libpq: a row is ready to store */
    W_EV_ROW_STORED, /* the glue stored it; now_ms is the time */
    W_EV_CHUNK_FULL, /* the row did not fit the current chunk; after the actions,
                        deliver the same W_EV_ROW again */
    W_EV_TAG,       /* libpq: a statement completed with a tag */
    W_EV_ERROR,     /* libpq: an error result */
    W_EV_END,       /* libpq: no more results for the query */
    W_EV_LOST,      /* libpq: the connection dropped */
    W_EV_TICK,      /* the poll timer; now_ms is the time */
    W_EV_CANCEL_FAILED, /* libpq: the cancel request failed or passed its deadline */
    W_EV_CANCEL_DONE   /* libpq: the server has the cancel request */
} conn_worker_event_kind;

typedef struct {
    conn_worker_event_kind kind;
    size_t arena;
    proto_header h;
    uint64_t aux;
    uint64_t now_ms;
} conn_worker_event;

#define CONN_FIRST_CHUNK_ROWS 100u
#define CONN_FLUSH_MS 250u

typedef struct {
    uint32_t conn;
    worker_state state;
    size_t reserved;   /* the worker's reserved arena */
    bool have_reserved;
    bool lost_pending; /* LOST waits for the reserved arena */
    bool cancel_failed_pending; /* CANCEL_FAILED waits for the reserved arena */
    bool bye_pending;  /* BYE waits for the reserved arena */
    /* The live request. */
    uint64_t req;
    size_t terminal;
    uint64_t row_limit;
    size_t quota;
    sc_arena *book;  /* holds `grants` */
    sc_tslice grants; /* unused granted arenas, a stack */
    size_t grant_count;
    bool have_chunk;  /* a chunk is open */
    size_t chunk;
    uint64_t chunk_rows;
    uint64_t chunk_first_ms;
    bool sent_chunk;  /* a CHUNK went out in this set */
    size_t sent_in_set; /* arenas sent in this set: COLUMNS and CHUNKs */
    uint64_t rows;    /* rows stored in this set */
    uint64_t tags;    /* statements completed */
    bool in_set;      /* the current statement has columns */
    proto_cap cap;
    bool capped;      /* the cancel came from a cap */
    bool dropped;     /* rows were discarded during a cancel */
    bool cancel_out;  /* libpq's cancel is under way (ACT_CANCEL, not yet DONE or FAILED) */
    bool query_held;  /* the live run's query waits for that cancel */
} conn_worker;

/* libpq 17+'s cancel is non-blocking (KTD7), so it can still be under way
 * when its query ends on its own. A RUN that arrives then is held: its query
 * goes out (ACT_QUERY) once W_EV_CANCEL_DONE or W_EV_CANCEL_FAILED says the
 * cancel is over, so the cancel cannot land on it. A CANCEL for a held run
 * ends it at once, with no rows. The glue reports DONE after every cancel
 * that went through, blocking ones included.
 *
 * A worker that has received nothing. `book` holds the grant stack, sized by
 * `max_grants`: at least the smaller of the window and quota + 1. */
bool conn_worker_init(conn_worker *w, sc_arena *book, uint32_t conn, size_t max_grants);

/* Steps the machine. False means a broken invariant, as for conn_ui_step. */
bool conn_worker_step(conn_worker *w, conn_worker_event e, conn_actions *out);

#endif
