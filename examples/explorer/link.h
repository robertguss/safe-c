#ifndef EXPLORER_LINK_H
#define EXPLORER_LINK_H

/* The UI thread's side of one connection: the wires, the worker thread and
 * the conn_ui machine (conn.h). Glue code, shared by main.c and
 * tests/explorer_pq_integration.c.
 *
 * Every connection draws on one result pool (link_pool), which main.c maps
 * and which outlives its links: each link takes its arenas from its owner's
 * budget (budget.h, KTD19), and a result a tab keeps belongs to the tab, so a
 * link can go and a rebuilt one finds the tab's arenas untouched.
 *
 * Commands step the machine and perform its actions at once. link_poll drains
 * the replies: call it whenever the worker rings, and on a timeout while a
 * query is active (KTD6), since a ring can be lost. */

#include "budget.h"
#include "result.h"
#include "worker.h"

#define LINK_ARENA_BYTES (256u * 1024u) /* one result chunk (KTD11) */
#define LINK_TEXT_BYTES (128u * 1024u)  /* each private arena: books, handles, wires, texts */
/* The result index (result_book_bytes) gets one too, which bounds the quota
 * at 152 arenas; link_init fails past it. */

/* The result pool every connection shares, its budget and its book. */
typedef struct {
    sc_mem_pool arenas; /* result arenas, indexed by the messages */
    sc_mem_pool priv;   /* one arena: the budget's records and the book */
    budget budget;
    conn_pool book;
} link_pool;

/* Maps a pool of the split's sum. link_pool_free is needed after a failure
 * too. */
bool link_pool_init(link_pool *p, budget_split split);
/* A kept result's arena `arena` goes back to the pool (conn_pool_free_kept),
 * emptied. No link is needed, so a tab frees its result after its link is
 * gone. */
bool link_pool_free_kept(link_pool *p, size_t arena);
/* Arenas taken from the pool, by every connection and every kept result. */
size_t link_pool_used(const link_pool *p);
/* Unmaps the pool: every link on it is freed first. */
void link_pool_free(link_pool *p);

typedef struct {
    link_pool *shared; /* the pool this connection draws on */
    sc_mem_pool priv; /* this connection's private arenas */
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
    bool retired;     /* link_retire went through: the UI no longer reads it */
    result res;       /* the current set, kept in step with ui.held */
    result idle;      /* an empty result on the link's own book, for when no tab owns `res` */
    uint64_t res_req, res_sets; /* the request and set `res` holds */
} conn_link;

/* A connection on `shared`, charged to `owner`, not yet open. `conn` is
 * nonzero and differs from every other link on the pool, which books each
 * arena by it. Its mailbox and
 * request channel hold every arena the owner's budget allows plus its two
 * control arenas, so neither can fill (KTD5). `ring` is how the worker wakes
 * the UI thread. */
bool link_init(conn_link *l, link_pool *shared, budget_owner owner, uint32_t conn,
               conn_limits lim, worker_ring_fn ring, void *ring_arg);

/* Starts the worker and connects with `conninfo` (live in `a`; empty uses the
 * PG* environment). */
bool link_open(conn_link *l, const sc_arena *a, sc_str conninfo);

/* Runs `sql` (live in `a`), or queues it while connecting. A refusal (pool
 * exhausted, busy, not connected) sets `refused` and `refusal`. */
bool link_run(conn_link *l, const sc_arena *a, sc_str sql);
/* The same with a row limit for this run: 0 is the connection's, UINT64_MAX
 * leaves only the quota ("Load all"). */
bool link_run_limit(conn_link *l, const sc_arena *a, sc_str sql, uint64_t row_limit);
bool link_cancel(conn_link *l);

/* Tabs (plan U12). A result is a value over its own book arena, so it moves
 * between the link and a tab by copy; only one copy is ever filled.
 * - link_adopt: `r` (empty, quota-sized, on the caller's book) becomes the
 *   result the next run fills. Call it with nothing held: before the first
 *   run, or after link_keep or link_abandon.
 * - link_keep: the finished result passes to the caller in `*kept`, which
 *   frees it later with link_pool_free_kept: each index result_close
 *   returns, and the terminal arena of the conn_ui copied before the call
 *   when it was kept. `res` goes back to `idle`. `*kept` is written even when
 *   the link is broken.
 * - link_abandon: the owner went away. Its arenas are freed, a live run is
 *   cancelled and its late replies freed as they arrive (KTD5). */
void link_adopt(conn_link *l, result r);
bool link_keep(conn_link *l, result *kept);
bool link_abandon(conn_link *l);

/* Sends CLOSE. The worker answers BYE and returns; then ui.state is
 * UI_CONN_CLOSED and link_join can join it. */
bool link_close(conn_link *l);

/* Disconnect and Reconnect (plan U13, KTD19): the UI stops reading the link
 * and it retires. Its current result passes to the caller in `*kept`, as
 * link_keep passes it, a live or queued run ends Disconnected, CLOSE goes
 * out, and what the link still has out is charged to the retiring owner until
 * it comes home. Keep calling link_poll until link_ended, then join and free
 * it. False, with nothing changed, while both retiring slots are busy
 * (`refusal` CONN_REFUSE_RETIRING). A broken link only has its worker told
 * to return; `*kept` is not written. */
bool link_retire(conn_link *l, result *kept);

/* Whether the worker is done with the link: BYE arrived, or the thread
 * returned and link_poll has seen it, or it never started. link_join then
 * does not block. */
bool link_ended(conn_link *l);

/* The UI's clock moved: a cancel that has run 5 s offers Disconnect, and one
 * past its deadline has failed. Call it while a query is active. */
void link_tick(conn_link *l);

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

/* Frees the link's private arenas and wake, and every pool arena the link
 * took, its own result's and any its worker held, BYE or not; a kept result
 * stays the tab's. Join first: a link whose worker still runs keeps its
 * arenas. Safe after a failed link_init, and needed then too. */
void link_free(conn_link *l);

#endif
