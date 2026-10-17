#ifndef SAFEC_CHAN_H
#define SAFEC_CHAN_H

#include "arena.h"
#include "handle.h"
#include <stdatomic.h>

/* A bounded single-producer, single-consumer channel that passes ownership of
 * a handle from a sender thread to a receiver thread, following
 * models/README.md. One thread sends and closes, one thread receives, and each
 * owns its own handle table: tables are not thread-safe, so a handle never
 * crosses threads. What crosses is the right to the slot, plus one size_t of
 * payload (an index into storage both threads can reach). Sending frees the
 * sender's slot, so every copy of the sender's handle goes stale and
 * sc_handle_get returns none for it. Receiving issues a fresh handle from the
 * receiver's table.
 * The ring is carved from an arena that the creating thread owns; the caller
 * must not reset that arena while the channel is in use, and must publish the
 * channel to the other thread with a synchronizing operation (thread start or
 * join). The fields are private.
 * This module is checked by tests, ThreadSanitizer (make tsan) and the TLA+
 * model, not by WP: the contracts below are documentation, and stdlib/chan.c
 * is excluded from `make prove`. */
typedef struct {
    size_t *ring;            /* cap + 1 entries: one stays empty to tell full from empty */
    size_t entries;          /* cap + 1 */
    _Atomic size_t head;     /* next entry to read; written by the receiver */
    _Atomic size_t tail;     /* next entry to write; written by the sender */
    atomic_bool closed;      /* set by the sender */
} sc_chan;

typedef enum {
    SC_CHAN_OK,     /* the message moved */
    SC_CHAN_FULL,   /* send: the queue is full; the sender keeps its handle */
    SC_CHAN_EMPTY,  /* recv: nothing queued and the channel is open */
    SC_CHAN_CLOSED, /* send: the channel is closed; recv: closed and drained */
    SC_CHAN_STALE,  /* send: the handle is not live in the sender's table */
    SC_CHAN_NOSLOT  /* recv: the receiver's table has no free slot; the message stays queued */
} sc_chan_status;

/* Carves room for cap messages from a. A zero or oversized capacity, or a full
 * arena, returns false and leaves *c untouched. */
bool sc_chan_new(sc_chan *c, sc_arena *a, size_t cap);

/* Sender side. Checks, in order: the handle is live in t (STALE), the channel
 * is open (CLOSED), the queue has room (FULL). Only OK frees the handle's
 * slot, so a refused send leaves the sender in possession. */
sc_chan_status sc_chan_send(sc_chan *c, const sc_arena *a, sc_handle_table t, sc_handle h,
                            size_t value);

/* Sender side. After close no send succeeds; queued messages stay receivable. */
void sc_chan_close(sc_chan *c);

/* Receiver side. On OK, *out is a live handle in t and *value the payload; on
 * any other status both are untouched. Closed channels still drain: CLOSED
 * means closed and empty. */
sc_chan_status sc_chan_recv(sc_chan *c, const sc_arena *a, sc_handle_table t, sc_handle *out,
                            size_t *value);

#endif
