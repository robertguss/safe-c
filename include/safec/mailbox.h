#ifndef SAFEC_MAILBOX_H
#define SAFEC_MAILBOX_H

#include "arena.h"
#include "handle.h"
#include <stdatomic.h>

/* A bounded multi-producer, single-consumer mailbox that passes ownership of a
 * handle from any number of sender threads to one receiver thread, following
 * models/README.md (models/mailbox.tla). It is the many-to-one counterpart of
 * sc_chan, and it passes ownership the same way: each thread owns its own
 * handle table, tables are not thread-safe, so a handle never crosses threads.
 * What crosses is the right to the slot, plus one size_t of payload (an index
 * into storage every thread can reach). A successful send frees the sender's
 * slot, so every copy of the sender's handle goes stale and sc_handle_get
 * returns none for it. Receiving issues a fresh handle from the receiver's
 * table.
 *
 * No call blocks or waits. A sender that finds the mailbox full gets
 * SC_MBOX_FULL and keeps its handle; it may retry or drop the message.
 *
 * Closing is a receiver action. sc_mbox_close fixes the final message count
 * with one atomic step: a send that claimed its place before that step is
 * delivered, and every send after it returns SC_MBOX_CLOSED and keeps its
 * handle. So a sender racing with close either delivers or keeps; it never
 * loses its handle without delivering. After close the receiver drains: recv
 * returns SC_MBOX_EMPTY while an accepted send is still being published, and
 * SC_MBOX_CLOSED only once every accepted message has been received.
 *
 * Memory is carved from an arena that the creating thread owns; the caller
 * must not reset that arena while the mailbox is in use, and must publish the
 * mailbox to the other threads with a synchronizing operation (thread start or
 * join). Capacity must be a power of two. The fields are private. Only one
 * thread at a time may call recv or close; handing the receiver role to
 * another thread needs a synchronizing operation too.
 * This module is checked by tests, ThreadSanitizer (make tsan), GenMC
 * (make genmc), the TLA+ model and TLAPS (make tlaps), not by WP: the
 * contracts below are documentation, and stdlib/mailbox.c is excluded from
 * `make prove`. */
typedef struct {
    _Atomic size_t seq; /* 2p: free for position p; 2p + 1: p published, unread; see mailbox.c */
    size_t value;
} sc_mbox_slot;

typedef struct {
    sc_mbox_slot *slots;
    size_t cap;         /* a power of two */
    _Atomic size_t tail; /* (claimed count << 1) | closed; written by senders and close */
    size_t head;        /* messages received; owned by the receiver */
} sc_mbox;

typedef enum {
    SC_MBOX_OK,     /* the message moved */
    SC_MBOX_FULL,   /* send: the mailbox is full; the sender keeps its handle */
    SC_MBOX_EMPTY,  /* recv: nothing ready and the mailbox is open, or accepted sends are in flight */
    SC_MBOX_CLOSED, /* send: the mailbox is closed; recv: closed and every accepted message received */
    SC_MBOX_STALE,  /* send: the handle is not live in the sender's table */
    SC_MBOX_NOSLOT  /* recv: the receiver's table has no free slot; the message stays queued */
} sc_mbox_status;

/* Carves room for cap messages from a. A capacity that is zero, not a power of
 * two or too large, or a full arena, returns false and leaves *m untouched. */
bool sc_mbox_new(sc_mbox *m, sc_arena *a, size_t cap);

/* Sender side, callable from any number of threads at once. Checks, in order:
 * the handle is live in t (STALE), the mailbox is open (CLOSED), a slot is
 * free (FULL). Only OK frees the handle's slot, so a refused send leaves the
 * sender in possession. Messages from one sender arrive in send order;
 * messages from different senders arrive in the order their sends claimed a
 * place. */
sc_mbox_status sc_mbox_send(sc_mbox *m, const sc_arena *a, sc_handle_table t, sc_handle h,
                            size_t value);

/* Receiver side. After close no send succeeds; accepted messages stay
 * receivable. Idempotent. */
void sc_mbox_close(sc_mbox *m);

/* Receiver side. On OK, *out is a live handle in t and *value the payload; on
 * any other status both are untouched. */
sc_mbox_status sc_mbox_recv(sc_mbox *m, const sc_arena *a, sc_handle_table t, sc_handle *out,
                            size_t *value);

#endif
