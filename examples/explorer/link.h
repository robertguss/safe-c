#ifndef EXPLORER_LINK_H
#define EXPLORER_LINK_H

/* The UI thread's side of one connection: the result pool, the wires, the
 * worker thread and the conn_ui machine (conn.h). Glue code, shared by main.c
 * and tests/explorer_pq_integration.c.
 *
 * Each connection owns its pool and mailbox, where the plan (KTD11) has one
 * pool for the app. Milestone 2 has one connection; sharing the pool waits
 * for tabs (U13), and is logged in BREAKAGE.md.
 *
 * Commands step the machine and perform its actions at once. link_poll drains
 * the replies: call it whenever the worker rings, and on a timeout while a
 * query is active (KTD6), since a ring can be lost. */

#include "worker.h"

#define LINK_ARENA_BYTES (256u * 1024u) /* one result chunk (KTD11) */
#define LINK_TEXT_BYTES (128u * 1024u)  /* each private arena: books, handles, wires, texts */

typedef struct {
    sc_mem_pool pool; /* result arenas, indexed by the messages */
    sc_mem_pool priv; /* this connection's private arenas */
    conn_pool book;
    conn_ui ui;
    conn_actions acts;
    wire_side side;
    sc_chan requests;
    sc_mbox replies;
    sc_wake wake;     /* the worker's */
    sc_flag pending;  /* the UI's doorbell flag */
    worker_ctx wctx;
    sc_thread thread;
    sc_str conninfo;  /* in its own private arena */
    sc_str sql;       /* the last SQL run or refused, kept for a queued run */
    sc_str failure;   /* the last CONN_FAILED message */
    bool refused;     /* the last command was refused */
    conn_refuse refusal;
    bool started;     /* the worker thread is running and not yet joined */
    bool broken;      /* a broken invariant; the link is unusable */
} conn_link;

/* A connection with `arenas` result arenas, not yet open. `ring` is how the
 * worker wakes the UI thread. */
bool link_init(conn_link *l, uint32_t conn, size_t arenas, conn_limits lim, worker_ring_fn ring,
               void *ring_arg);

/* Starts the worker and connects with `conninfo` (live in `a`; empty uses the
 * PG* environment). */
bool link_open(conn_link *l, const sc_arena *a, sc_str conninfo);

/* Runs `sql` (live in `a`), or queues it while connecting. A refusal (pool
 * exhausted, busy, not connected) sets `refused` and `refusal`. */
bool link_run(conn_link *l, const sc_arena *a, sc_str sql);
bool link_cancel(conn_link *l);

/* Sends CLOSE. The worker answers BYE and returns; then ui.state is
 * UI_CONN_CLOSED and link_join can join it. */
bool link_close(conn_link *l);

/* Drains every reply. True when any arrived. Sets `broken` when the worker
 * returned without BYE. */
bool link_poll(conn_link *l);

/* Joins the worker. Call it once ui.state is UI_CONN_CLOSED (BYE arrived), or
 * when the link is broken or OPEN was refused: then it closes the request
 * channel, which makes the worker return. False if it never started, ended
 * without BYE, or reported a broken invariant. */
bool link_join(conn_link *l);

/* The pool arena at `i`, for reading a held reply. */
sc_arena *link_arena(const conn_link *l, size_t i);

/* The arena `failure` lives in. */
const sc_arena *link_failure_arena(const conn_link *l);

/* Unmaps the arenas and closes the wake. Join first. Safe after a failed
 * link_init, and needed then too. */
void link_free(conn_link *l);

#endif
