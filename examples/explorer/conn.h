#ifndef EXPLORER_CONN_H
#define EXPLORER_CONN_H

/* The connection and query lifecycle as two state machines: one for the UI
 * thread (conn_ui) and one for a worker (conn_worker). Each is a step function
 * from (state, event) to (state, actions). The actions are data, so tests
 * assert them and the glue (main.c, worker.c) only performs them: it writes
 * and sends the messages, resets arenas and calls libpq.
 *
 * Core code: it includes only the stdlib, proto.h and budget.h.
 *
 * Arena rules the machines keep (plan KTD5):
 * - The UI is the only allocator. conn_pool books who holds each pool arena,
 *   for every connection that shares the pool, and its budget (budget.h,
 *   KTD19) says which class pays for each arena taken.
 * - Each open connection holds two reserved arenas for its life: the UI's
 *   control arena (CANCEL, CLOSE) and the worker's (CONNECTED, CONN_FAILED,
 *   LOST, CANCEL_FAILED, BYE). The UI hands the worker's back after reading each reply, and
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
 * - A reply whose request is not the live one is dropped and its arena freed.
 * - A retired connection (U13, KTD19) is one the UI no longer reads: what it
 *   still holds is charged to BUDGET_RETIRING, every reply but its arenas'
 *   way home is dropped, and BYE ends it. One that never says BYE gives its
 *   arenas back when the UI reclaims them after its worker returned. */

#include "budget.h"
#include "proto.h"

/* Who holds a pool arena, from the UI's side. */
typedef enum {
    POOL_FREE,
    POOL_UI,     /* the UI: a result, or a reserved arena at home */
    POOL_WORKER, /* sent to a worker and not yet back */
    POOL_KEPT    /* a finished result a tab keeps: no connection's any more */
} pool_owner;

/* The UI's book of the pool: one owner byte and the connection id per arena,
 * and the budget that pays for them. One book serves every connection on the
 * pool, so connection ids must differ, and be nonzero. */
typedef struct {
    sc_arena *arena; /* holds the book */
    budget *budget;
    sc_tslice owner;
    sc_tslice conn;  /* uint32_t: the connection that took each arena */
    size_t count;
    size_t free;
    size_t retiring; /* connections retired and not yet ended (BYE or reclaim) */
} conn_pool;

/* A book for the pool `b` budgets, every arena free; `a` holds it. */
bool conn_pool_init(conn_pool *p, sc_arena *a, budget *b);
/* The owner of arena `i`, or POOL_FREE for an index past the pool. */
pool_owner conn_pool_owner(const conn_pool *p, size_t i);
/* Kept arena `i` goes back to the pool and its budget. False when it is not
 * kept. The caller resets the arena. No connection is needed, so a result
 * outlives the link that fetched it. */
bool conn_pool_free_kept(conn_pool *p, size_t i);
/* Whether a connection can retire now: fewer than BUDGET_RETIRING_SLOTS are. */
bool conn_pool_can_retire(const conn_pool *p);

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
    CONN_REFUSE_RETIRING,      /* both retiring slots are busy (KTD19) */
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

/* Plan U13: Disconnect is offered after this long in Cancelling, and a cancel
 * with no reply by the deadline has failed. The worker keeps the same
 * deadline for libpq's non-blocking cancel (KTD7). */
#define CONN_DISCONNECT_OFFER_MS 5000u
#define CONN_CANCEL_DEADLINE_MS 10000u

typedef struct {
    size_t window;      /* chunk arenas granted with a RUN */
    size_t quota;       /* arenas one result set may hold: its COLUMNS and CHUNKs */
    uint64_t row_limit; /* rows one result set may hold */
} conn_limits;

typedef struct {
    uint32_t conn;
    budget_owner owner; /* who the budget charges for this connection's arenas */
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
    bool abandoned;     /* its owner went away: data is freed on arrival */
    uint64_t run_limit; /* the row limit of the next or live run */
    /* The user's cancel (U13). */
    uint64_t cancel_ms;    /* when the query went to Cancelling */
    bool offer_disconnect; /* Cancelling long enough, or failed: offer Disconnect */
    bool cancel_failed;    /* the deadline passed, or the worker said so */
    bool retiring;         /* retired, and not yet ended by BYE or a reclaim */
} conn_ui;

/* A UI event: a command, or a decoded message from the worker (header fields
 * plus the arena it came in), and the UI's clock. The glue reads a message's
 * body before performing the step's actions, since they may hand the arena
 * back.
 *
 * RUN takes its row limit from h.arg, or the connection's when 0 (KTD11:
 * "Load all" runs with UINT64_MAX, so only the quota stops it).
 *
 * KEEP needs a finished query: the held arenas and a kept terminal arena
 * become POOL_KEPT in the book and the machine forgets them, so the next RUN
 * frees nothing; the state fields still describe that result until then.
 * conn_pool_free_kept returns each one. It cannot tell one caller's kept arena
 * from another's, so each caller must free exactly the indices its own result
 * kept.
 * ABANDON frees the current result at once; a live run is cancelled, and its
 * later COLUMNS, CHUNKs and terminal reply are freed as they arrive.
 *
 * RETIRE (U13, KTD19): the user disconnects, or rebuilds the connection. The
 * current result passes to the caller as KEEP passes it, a live or queued run
 * ends Disconnected, CLOSE goes out, and everything else the connection holds
 * is charged to BUDGET_RETIRING until it comes home; BYE ends the retirement.
 * Refused (CONN_REFUSE_RETIRING, nothing changed) while both slots are busy.
 * With no worker (never opened, or closed) it only passes the result on.
 * TICK moves a run that has been Cancelling 5 s to offering Disconnect, and
 * one at the deadline to cancel failed.
 * RECLAIM: the worker returned without BYE and is joined. Every arena the
 * connection took comes back except kept ones, and it reads as closed. */
typedef enum {
    CONN_UI_EV_OPEN,   /* the user connects */
    CONN_UI_EV_RUN,    /* the user runs the SQL */
    CONN_UI_EV_CANCEL, /* the user cancels the query */
    CONN_UI_EV_CLOSE,  /* quit: close the connection */
    CONN_UI_EV_MSG,    /* a message arrived */
    /* Tabs (plan U12): several results share one connection; every
     * connection shares the pool (KTD19). */
    CONN_UI_EV_KEEP,    /* the finished result's arenas pass to the caller */
    CONN_UI_EV_ABANDON, /* the result's owner went away: free it, cancel a live run */
    /* Plan U13. */
    CONN_UI_EV_RETIRE,  /* disconnect: stop reading, close, charge the rest to retiring */
    CONN_UI_EV_TICK,    /* time passed: the cancel's offer and deadline */
    CONN_UI_EV_RECLAIM  /* the worker returned without BYE: take every arena back */
} conn_ui_event_kind;

typedef struct {
    conn_ui_event_kind kind;
    size_t arena;
    proto_header h;
    uint64_t now_ms; /* the UI's clock, for CANCEL and TICK */
} conn_ui_event;

/* A connection with no worker yet, whose arenas the budget charges to
 * `owner`. `book` holds the held-arena list, sized by `lim.quota`. */
bool conn_ui_init(conn_ui *c, sc_arena *book, uint32_t conn, budget_owner owner,
                  conn_limits lim);

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
    W_EV_TICK,      /* the poll timer; now_ms is the time */
    W_EV_CANCEL_FAILED /* libpq: the cancel request failed or passed its deadline */
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
} conn_worker;

/* A worker that has received nothing. `book` holds the grant stack, sized by
 * `max_grants`: at least the smaller of the window and quota + 1. */
bool conn_worker_init(conn_worker *w, sc_arena *book, uint32_t conn, size_t max_grants);

/* Steps the machine. False means a broken invariant, as for conn_ui_step. */
bool conn_worker_step(conn_worker *w, conn_worker_event e, conn_actions *out);

#endif
