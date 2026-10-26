#ifndef EXPLORER_CONN_H
#define EXPLORER_CONN_H

/* The connection and query lifecycle as two state machines: one for the UI
 * thread (conn_ui) and one for a worker (conn_worker). Each is a step function
 * from (state, event) to (state, actions). The actions are data, so tests
 * assert them and the glue (main.c, worker.c) only performs them: it writes
 * and sends the messages, resets arenas and calls libpq.
 *
 * Core code: it includes only the stdlib and proto.h.
 *
 * Arena rules the machines keep (plan KTD5):
 * - The UI is the only allocator. conn_pool books who holds each pool arena.
 * - Each open connection holds two reserved arenas for its life: the UI's
 *   control arena (CANCEL, CLOSE) and the worker's (CONNECTED, CONN_FAILED,
 *   LOST, BYE). The UI hands the worker's back after reading each reply, and
 *   the worker always returns the control arena.
 * - A RUN's own arena is the request's terminal arena, so a terminal reply
 *   never waits for credit. The UI keeps up to `window` chunk arenas with the
 *   worker while the current set holds at most `quota` arenas, so one credit
 *   past the quota stays with the worker for the next statement's COLUMNS. A
 *   second COLUMNS frees the earlier set and tops the credit back up (KTD12).
 *   No grant goes out once CLOSE has.
 * - Before its terminal reply, a worker returns every granted arena it holds.
 *   A GRANT still in flight crosses the reply and comes back after it as a
 *   RETURN under the same request id, which frees it like any other.
 * - A reply whose request is not the live one is dropped and its arena freed. */

#include "proto.h"

/* Who holds a pool arena, from the UI's side. */
typedef enum {
    POOL_FREE,
    POOL_UI,     /* the UI: a result, or a reserved arena at home */
    POOL_WORKER, /* sent to a worker and not yet back */
} pool_owner;

/* The UI's book of the pool: one owner byte per arena. */
typedef struct {
    sc_arena *arena; /* holds the book */
    sc_tslice owner;
    size_t count;
    size_t free;
} conn_pool;

bool conn_pool_init(conn_pool *p, sc_arena *a, size_t count);
/* The owner of arena `i`, or POOL_FREE for an index past the pool. */
pool_owner conn_pool_owner(const conn_pool *p, size_t i);

/* What the glue does. */
typedef enum {
    ACT_NONE,
    /* UI and worker: write a message of `msg` kind into `arena` (header from
     * conn, req, arg, aux) and send it. The body comes from the glue: the
     * conninfo, SQL, column names, rows, tags or error text. */
    ACT_SEND,
    ACT_FREE,      /* UI: reset `arena`; it is back in the pool */
    ACT_DROP,      /* UI: a stale reply; reset `arena`, it is back in the pool */
    ACT_RESET,     /* UI: reset `arena`, a reserved arena back at home */
    ACT_REFUSE,    /* UI: the command was not sent; `aux` is a CONN_REFUSE_* reason */
    ACT_CONNECT,   /* worker: start connecting with the conninfo in `arena` */
    ACT_QUERY,     /* worker: send the SQL in `arena` (the terminal arena) */
    ACT_STORE_ROW, /* worker: store the ready row in chunk `arena`; arg 1: a new chunk */
    ACT_WAIT,      /* worker: no credit yet; deliver this event again after the next message */
    ACT_DISCARD,   /* worker: drop this result row or column set */
    ACT_CANCEL,    /* worker: ask the server to cancel the query */
    ACT_FINISH,    /* worker: close the libpq connection */
    ACT_EXIT       /* worker: the loop ends; the thread returns */
} act_kind;

typedef enum {
    CONN_REFUSE_POOL,          /* the pool has no free arena for it */
    CONN_REFUSE_BUSY,          /* a query is already active */
    CONN_REFUSE_NOT_CONNECTED, /* no live connection */
} conn_refuse;

typedef struct {
    act_kind kind;
    proto_kind msg;
    size_t arena;
    uint32_t conn;
    uint64_t req;
    uint64_t arg;
    uint64_t aux;
} conn_action;

/* The actions of one step, in order. The first failure sticks. */
typedef struct {
    sc_arena *arena;
    sc_tslice items;
    size_t count;
    bool failed;
} conn_actions;

bool conn_actions_init(conn_actions *out, sc_arena *a, size_t cap);
void conn_actions_clear(conn_actions *out);
/* Action `i`, or one of kind ACT_NONE past the end. */
conn_action conn_action_at(const conn_actions *out, size_t i);

/* ---- UI side ---- */

typedef enum {
    UI_CONN_NONE,
    UI_CONN_CONNECTING,
    UI_CONN_CONNECTED,
    UI_CONN_AUTH_FAILED,
    UI_CONN_UNREACHABLE,
    UI_CONN_DISCONNECTED,
    UI_CONN_CLOSING,
    UI_CONN_CLOSED
} ui_conn_state;

/* The query states the UI renders (plan R3, the High-Level Technical Design). */
typedef enum {
    Q_NONE,
    Q_QUEUED,
    Q_RUNNING,
    Q_STREAMING,
    Q_CANCELLING,
    Q_ROWS,
    Q_COMMAND_TAG,
    Q_ERROR,
    Q_ERROR_AFTER_ROWS,
    Q_CAPPED,
    Q_CANCELLED,
    Q_DISCONNECTED
} query_state;

typedef struct {
    size_t window;      /* chunk arenas granted with a RUN */
    size_t quota;       /* arenas one result set may hold: its COLUMNS and CHUNKs */
    uint64_t row_limit; /* rows one result set may hold */
} conn_limits;

typedef struct {
    uint32_t conn;
    ui_conn_state state;
    conn_limits lim;
    bool reserved;       /* ctl and wrk are taken from the pool */
    size_t ctl;          /* the UI's control arena */
    bool ctl_home;       /* it is with the UI */
    size_t wrk;          /* the worker's reserved arena */
    bool close_pending;  /* CLOSE waits for the control arena */
    bool cancel_pending; /* CANCEL waits for the control arena */
    proto_cap cancel_why;
    /* The live query. */
    query_state q;
    uint64_t req;      /* its request id; 0 before the first run */
    uint64_t next_req;
    size_t terminal;   /* the RUN arena, which holds the terminal reply */
    bool terminal_kept; /* the terminal reply arrived and the result holds it */
    bool terminal_out;  /* the terminal arena is with the worker */
    size_t outstanding; /* granted chunk arenas with the worker */
    sc_arena *book;    /* holds `held` */
    sc_tslice held;     /* the current set's arenas: COLUMNS, then CHUNKs */
    size_t held_count;
    uint64_t rows;      /* rows in the current set */
    uint64_t sets;      /* column sets seen; a later one replaces the earlier */
    uint64_t earlier_tags; /* from the terminal reply's aux */
    proto_cap cap;      /* why it ended CAPPED */
    bool cancel_sent;   /* a CANCEL for it went out */
} conn_ui;

/* A UI event: a command, or a decoded message from the worker (header fields
 * plus the arena it came in). The glue reads a message's body before
 * performing the step's actions, since they may hand the arena back. */
typedef enum {
    CONN_UI_EV_OPEN,   /* the user connects */
    CONN_UI_EV_RUN,    /* the user runs the SQL */
    CONN_UI_EV_CANCEL, /* the user cancels the query */
    CONN_UI_EV_CLOSE,  /* quit: close the connection */
    CONN_UI_EV_MSG     /* a message arrived */
} conn_ui_event_kind;

typedef struct {
    conn_ui_event_kind kind;
    size_t arena;
    proto_header h;
} conn_ui_event;

/* A connection with no worker yet. `book` holds the held-arena list, sized by
 * `lim.quota`. */
bool conn_ui_init(conn_ui *c, sc_arena *book, uint32_t conn, conn_limits lim);

/* Steps the machine. False means a broken invariant (an action list too small,
 * or a message that breaks the protocol, such as an arena the book says the
 * worker does not hold); the actions so far still describe arena moves the
 * glue must perform. */
bool conn_ui_step(conn_ui *c, conn_pool *p, conn_ui_event e, conn_actions *out);

/* Whether a query is queued or under way: QUEUED, RUNNING, STREAMING or
 * CANCELLING. */
bool conn_ui_active(const conn_ui *c);

/* The held arena `i` of the current set, for the view: SIZE_MAX past the end. */
size_t conn_ui_held(const conn_ui *c, size_t i);

/* ---- Worker side ---- */

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
    W_EV_TICK       /* the poll timer; now_ms is the time */
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
} conn_worker;

/* A worker that has received nothing. `book` holds the grant stack, sized by
 * `max_grants`: at least the smaller of the window and quota + 1. */
bool conn_worker_init(conn_worker *w, sc_arena *book, uint32_t conn, size_t max_grants);

/* Steps the machine. False means a broken invariant, as for conn_ui_step. */
bool conn_worker_step(conn_worker *w, conn_worker_event e, conn_actions *out);

#endif
