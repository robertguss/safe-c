#ifndef EXPLORER_META_H
#define EXPLORER_META_H

/* The metadata link's driver (plan U18, KTD21): it opens the link once the
 * query link connects, restarts the catalog on each new link, runs the
 * catalog's reads on it one statement per RUN, and hands each finished
 * result to the catalog, then back to the pool. Glue code, shared by main.c
 * and tests/explorer_pq_integration.c; the link itself lives in the link set
 * (links.h), which retires it with the query link on Disconnect and
 * Reconnect. */

#include "catalog.h"
#include "links.h"

typedef struct {
    catalog cat;
    link_set *links;
    sc_arena *sql;      /* the running read's SQL, and a failure's message */
    bool running;       /* a read is out on the link */
    catalog_read read;
    uint32_t conn;      /* the metadata link the catalog last restarted on; 0 for none */
} meta_side;

/* A driver for the set's metadata link, with the catalog's records in
 * `records`, its kept names in `keep` and SQL in `sql`. */
bool meta_init(meta_side *m, link_set *s, sc_arena *records, sc_arena *keep, sc_arena *sql);

/* Call whenever a worker rings, and on the UI's timeouts: opens the link
 * when it is due, drains its replies, stores a finished read and starts the
 * next. True when what the sidebar shows may have changed. */
bool meta_poll(meta_side *m);

/* The two links' states, as the sidebar reads them (catalog_shown). */
catalog_link meta_query_link(link_set *s);
catalog_link meta_link(meta_side *m);
catalog_show meta_shown(meta_side *m);

/* Why the sidebar shows an error: the metadata link was lost, or failed to
 * connect (`*msg` is libpq's message, live in `*a`), or the session or schema
 * list read failed (`*msg` in the catalog). */
typedef enum { META_ERROR_NONE, META_ERROR_LOST, META_ERROR_CONNECT, META_ERROR_CATALOG } meta_error;
meta_error meta_error_of(meta_side *m, const sc_arena **a, sc_str *msg);

/* What the sidebar's error says (`*text`, live in `*a`) and whether its Retry
 * can run now (`*retry`; when not, it reads as waiting for the old
 * connection). A lost link and an empty message get fixed texts, written in
 * `fa`. False when the sidebar shows no error. */
bool meta_sidebar_error(meta_side *m, sc_arena *fa, const sc_arena **a, sc_str *text, bool *retry);

/* The sidebar's Retry: a failed metadata link retires and a new one opens on
 * the next poll; a failed schema list is read again on the live link, which
 * needs no retiring slot. False when there is nothing to retry, or the failed
 * link has no retiring slot free (meta_can_retry). */
bool meta_retry(meta_side *m);
bool meta_can_retry(meta_side *m);

#endif
