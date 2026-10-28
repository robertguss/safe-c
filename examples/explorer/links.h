#ifndef EXPLORER_LINKS_H
#define EXPLORER_LINKS_H

/* The query window's connections (plans U13 and U18, KTD19, KTD21): the
 * current query link, the metadata link, and up to BUDGET_RETIRING_SLOTS
 * retired ones still draining, on one result pool. Glue code, shared by
 * main.c and tests/explorer_pq_integration.c. The set owns the slots, which
 * ones are current, the next connection id and whether the user
 * disconnected; main.c keeps the tabs, and meta.c drives the metadata link.
 *
 * Retiring the current link (Disconnect, Reconnect, or Cancel while it
 * connects) hands its live result to the caller in a link_handoff and puts a
 * fresh, unopened link in its place; the old one drains in its slot until its
 * worker is done. The metadata link retires with it, its result dropped, and
 * a new one opens only once a query link has connected again: Reconnect
 * rebuilds both. */

#include "link.h"

/* The query link, the metadata link and BUDGET_RETIRING_SLOTS retiring ones. */
#define LINKS_SLOTS 4u

/* The metadata link's limits: its budget (BUDGET_META_ARENAS) holds a RUN,
 * the set's quota and one credit past it. */
#define LINKS_META_WINDOW 2u
#define LINKS_META_QUOTA 12u
#define LINKS_META_ROWS 1000000u

/* A link and whether it is set up. A conn_link cannot move once its worker
 * runs (the worker holds pointers into it), so links live in fixed slots. */
typedef struct {
    conn_link link;
    bool used; /* link_init ran; link_free is owed */
} link_slot;

typedef struct {
    link_pool *pool;
    conn_limits lim;
    worker_ring_fn ring;
    void *ring_arg;
    const sc_arena *info_arena; /* holds `info`, the connection settings every link uses */
    sc_str info;
    const sc_arena *password_arena;
    sc_str password;
    link_slot s0, s1, s2, s3;
    size_t cur;         /* the slot of the current link */
    bool has_meta;      /* a metadata link is set up, in slot `meta` */
    size_t meta;
    uint32_t next_conn; /* the next link's id: each link on the pool has its own */
    bool offline;       /* the user disconnected: the current link is not opened yet */
} link_set;

/* What a retire hands the caller: the current link's conn_ui, copied before
 * the link forgot its arenas and ending as the retire left it; the request
 * and set its result holds; the result; and whether its run was still queued,
 * so never went out. */
typedef struct {
    conn_ui ended;
    uint64_t req, sets;
    result kept;
    bool unsent;
} link_handoff;

/* A set with no links on `pool`. Every link gets `lim` and `ring`, and opens
 * with `info` (live in `info_arena` while the set is used; empty uses the PG*
 * environment). */
void links_init(link_set *s, link_pool *pool, conn_limits lim, worker_ring_fn ring, void *ring_arg,
                const sc_arena *info_arena, sc_str info);

/* Sets up the first current link and opens it. */
bool links_open(link_set *s);

/* Whether a current link is set up, and the link. */
bool links_has_cur(const link_set *s);
conn_link *links_cur(link_set *s);

/* Whether the current link can retire now: retiring slots are free for it
 * and for a metadata link whose worker still runs, and a link slot is free
 * for the new one (KTD19). */
bool links_can_retire(link_set *s);

/* The metadata link, or NULL when none is set up. */
conn_link *links_meta(link_set *s);

/* Sets up and opens the metadata link (KTD21), once the current link is
 * connected and none is set up. A link that fails to set up stays, broken,
 * until links_meta_retire. */
bool links_meta_open(link_set *s);

/* The metadata link's Retry (plan U18): it retires, its result dropped, and
 * leaves no metadata link; one whose worker never started or has ended is
 * freed at once. False, with nothing changed, while no retiring slot is
 * free for a worker that still runs. */
bool links_meta_retire(link_set *s);
bool links_meta_can_retire(link_set *s);

/* Retires the current link into `*out` (link_retire) and makes a fresh,
 * unopened one current: the set is offline. False, with nothing changed,
 * when it cannot retire. */
bool links_retire(link_set *s, link_handoff *out);

/* Disconnect: retires a current link that connects or is connected. */
bool links_disconnect(link_set *s, link_handoff *out);

/* Reconnect: retires the current link unless offline (`*handed` says whether
 * `*out` was written), then opens the current link. Refused unless it can
 * retire, offline too, so the link it opens can be cancelled or disconnected
 * later. The caller decides whether Reconnect is offered. */
bool links_reconnect(link_set *s, link_handoff *out, bool *handed);

/* Cancel while the current link connects (KTD7): it retires (`*handed`), or,
 * with no retiring slot free, its queued run is cancelled. */
bool links_cancel_connecting(link_set *s, link_handoff *out, bool *handed);

/* Polls the retired links, and joins and frees each one whose worker is done.
 * The metadata link is polled by its driver (meta.c). */
void links_drain(link_set *s);

/* Whether any link's worker still runs. */
bool links_running(link_set *s);

/* Waits up to `ms` for a worker's ring. */
typedef void (*links_wait_fn)(void *arg, uint32_t ms);

/* Quit (plans U8 and U13): CLOSE on the current and metadata links (a broken
 * one has its worker told to return), then up to `ms` in total, waiting through `wait`,
 * for every worker, retiring ones included; then joins and frees each link
 * whose worker is done. False when a worker still runs (in a blocking cancel
 * or DNS lookup, say): its link is left as it is, so neither the pool nor the
 * set may be freed or reused, and the process exits around it. */
bool links_close(link_set *s, uint32_t ms, links_wait_fn wait, void *wait_arg);

#endif
