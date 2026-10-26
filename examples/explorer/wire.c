#include "wire.h"

static bool renew(wire_side *s) {
    if (!sc_arena_reset(s->arena)) {
        return false;
    }
    s->table = sc_handle_table_new(s->arena, s->slots);
    return s->table.cap == s->slots;
}

bool wire_side_init(wire_side *s, sc_arena *a, size_t slots) {
    s->arena = a;
    s->slots = slots;
    return slots > 0 && renew(s);
}

/* A fresh handle, renewing the table once if every slot has retired. */
static bool take(wire_side *s, sc_handle *h) {
    return sc_handle_alloc(s->arena, s->table, h) ||
           (renew(s) && sc_handle_alloc(s->arena, s->table, h));
}

bool wire_chan_send(wire_side *s, sc_chan *c, size_t value) {
    sc_handle h;
    return take(s, &h) && sc_chan_send(c, s->arena, s->table, h, value) == SC_CHAN_OK;
}

bool wire_mbox_send(wire_side *s, sc_mbox *m, size_t value) {
    sc_handle h;
    return take(s, &h) && sc_mbox_send(m, s->arena, s->table, h, value) == SC_MBOX_OK;
}

sc_chan_status wire_chan_recv(wire_side *s, sc_chan *c, size_t *value) {
    sc_handle h;
    sc_chan_status st = sc_chan_recv(c, s->arena, s->table, &h, value);
    if (st == SC_CHAN_NOSLOT && renew(s)) {
        st = sc_chan_recv(c, s->arena, s->table, &h, value);
    }
    if (st == SC_CHAN_OK) {
        /* A slot at SC_HANDLE_GEN_MAX retires here; take renews the table. */
        (void)sc_handle_free(s->arena, s->table, h);
    }
    return st;
}

sc_mbox_status wire_mbox_recv(wire_side *s, sc_mbox *m, size_t *value) {
    sc_handle h;
    sc_mbox_status st = sc_mbox_recv(m, s->arena, s->table, &h, value);
    if (st == SC_MBOX_NOSLOT && renew(s)) {
        st = sc_mbox_recv(m, s->arena, s->table, &h, value);
    }
    if (st == SC_MBOX_OK) {
        (void)sc_handle_free(s->arena, s->table, h);
    }
    return st;
}
