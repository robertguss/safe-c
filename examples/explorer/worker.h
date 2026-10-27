#ifndef EXPLORER_WORKER_H
#define EXPLORER_WORKER_H

/* A connection's worker thread (plan U7). Glue code: it makes every libpq call
 * through adapters/sc_pq.h, performs the actions of the conn_worker machine
 * (conn.h), and moves arenas over the wires (wire.h).
 *
 * The loop waits on its libpq socket and its wake (KTD6), drains the request
 * channel, advances the connect or the query, and ticks the machine so a chunk
 * is sealed 250 ms after its first row (KTD11). After each reply it rings the
 * UI, but only when it moved the UI's pending flag from clear to set. */

#include "conn.h"
#include "../../adapters/sc_mem.h"
#include "../../adapters/sc_thread.h"
#include "wire.h"

/* How the worker wakes the UI thread: ui_wake in the app, a wake in tests. */
typedef void (*worker_ring_fn)(void *arg);

typedef struct {
    uint32_t conn;
    sc_mem_pool *pool;    /* the result arenas the messages index */
    sc_chan *requests;    /* from the UI */
    sc_mbox *replies;     /* to the UI */
    sc_wake wake;         /* rung by the UI after every send */
    sc_flag *pending;     /* the UI's doorbell flag */
    worker_ring_fn ring;
    void *ring_arg;
    sc_arena *book;       /* the machine's grant stack and action list */
    sc_arena *handles;    /* the worker's handle table */
    size_t max_grants;
    size_t max_actions;   /* the action list and the handle table: grants plus a step's own */
    bool broken;          /* a broken invariant stopped the loop; read after the join */
    sc_flag done;         /* set as the thread returns, then the UI is rung */
} worker_ctx;

/* The thread function: `arg` is a worker_ctx. Returns after it sends BYE, after
 * the request channel is closed, or after a broken invariant; it sets `done`
 * and rings the UI in every case. */
void worker_main(void *arg);

#endif
