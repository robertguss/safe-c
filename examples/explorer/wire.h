#ifndef EXPLORER_WIRE_H
#define EXPLORER_WIRE_H

/* One thread's end of the message wires (sc_chan, sc_mbox): its handle table
 * in an arena of its own (plan KTD5). Glue code.
 *
 * A message is one pool arena index. The sender takes a handle just before the
 * send, which frees it, and the receiver frees the handle it gets at once, so a
 * thread holds no handle between messages. That is what makes renewal safe:
 * when every slot has retired (SC_HANDLE_GEN_MAX reuses), the table's arena is
 * reset and a new table is built in it. */

#include "safec/chan.h"
#include "safec/mailbox.h"

typedef struct {
    sc_arena *arena;
    sc_handle_table table;
    size_t slots;
} wire_side;

/* A table of `slots` in `a`, which nothing else uses. */
bool wire_side_init(wire_side *s, sc_arena *a, size_t slots);

/* Sends `value` (an arena index) on a channel, or on a mailbox. False when the
 * send was refused: the queues are sized so that this is a bug (KTD5). */
bool wire_chan_send(wire_side *s, sc_chan *c, size_t value);
bool wire_mbox_send(wire_side *s, sc_mbox *m, size_t value);

/* Receives one message. SC_CHAN_OK or SC_MBOX_OK with *value set, or the
 * status that ended the drain (EMPTY, CLOSED). */
sc_chan_status wire_chan_recv(wire_side *s, sc_chan *c, size_t *value);
sc_mbox_status wire_mbox_recv(wire_side *s, sc_mbox *m, size_t *value);

#endif
