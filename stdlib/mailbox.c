#include "safec/arena.h"
#include "safec/handle.h"
#include "safec/mailbox.h"

bool sc_mbox_new(sc_mbox *m, sc_arena *a, size_t cap) {
    if (cap == 0 || (cap & (cap - 1)) != 0 || cap > SIZE_MAX / 4 / sizeof(sc_mbox_slot))
        return false;
    sc_slice s = sc_arena_alloc(a, cap * sizeof(sc_mbox_slot));
    if (s.len == 0)
        return false;
    m->slots = (sc_mbox_slot *)(void *)s.ptr;
    m->cap = cap;
    for (size_t i = 0; i < cap; i++) {
        atomic_init(&m->slots[i].seq, 2 * i);
        m->slots[i].value = 0;
    }
    atomic_init(&m->tail, 0);
    m->head = 0;
    return true;
}

/* A slot's seq counts two per position: 2p when it is free for position p, 2p + 1 once
 * p is published, then 2(p + cap) once p is read. Two per position keeps a published,
 * unread slot apart from a free one even at capacity 1. */

/* Claims the next place with one CAS on tail. Fails with CLOSED when the closed
 * bit is set and FULL when the slot at that place is still unread. */
static sc_mbox_status claim(sc_mbox *m, size_t *pos) {
    size_t t = atomic_load_explicit(&m->tail, memory_order_relaxed);
    for (;;) {
        if (t & 1)
            return SC_MBOX_CLOSED;
        size_t p = t >> 1;
        size_t seq = atomic_load_explicit(&m->slots[p & (m->cap - 1)].seq, memory_order_acquire);
        ptrdiff_t diff = (ptrdiff_t)(seq - 2 * p);
        if (diff < 0)
            return SC_MBOX_FULL;
        if (diff == 0 && atomic_compare_exchange_weak_explicit(&m->tail, &t, t + 2,
                                                               memory_order_relaxed,
                                                               memory_order_relaxed)) {
            *pos = p;
            return SC_MBOX_OK;
        }
        if (diff != 0)
            t = atomic_load_explicit(&m->tail, memory_order_relaxed);
    }
}

sc_mbox_status sc_mbox_send(sc_mbox *m, const sc_arena *a, sc_handle_table t, sc_handle h,
                            size_t value) {
    if (!sc_handle_get(a, t, h).some)
        return SC_MBOX_STALE;
    size_t pos;
    sc_mbox_status st = claim(m, &pos);
    if (st != SC_MBOX_OK)
        return st;
    /* The handle was just checked live and only this thread uses its table, so
     * freeing it cannot fail. The place is claimed, so the send must finish. */
    (void)sc_handle_free(a, t, h);
    sc_mbox_slot *slot = &m->slots[pos & (m->cap - 1)];
    slot->value = value;
    atomic_store_explicit(&slot->seq, 2 * pos + 1, memory_order_release);
    return SC_MBOX_OK;
}

void sc_mbox_close(sc_mbox *m) {
    atomic_fetch_or_explicit(&m->tail, 1, memory_order_release);
}

sc_mbox_status sc_mbox_recv(sc_mbox *m, const sc_arena *a, sc_handle_table t, sc_handle *out,
                            size_t *value) {
    size_t head = m->head;
    sc_mbox_slot *slot = &m->slots[head & (m->cap - 1)];
    if (atomic_load_explicit(&slot->seq, memory_order_acquire) != 2 * head + 1) {
        /* Nothing published at head. Closed with no claim at head is final: the
         * closed bit and the claimed count change together. */
        size_t tail = atomic_load_explicit(&m->tail, memory_order_acquire);
        if ((tail & 1) && (tail >> 1) == head)
            return SC_MBOX_CLOSED;
        return SC_MBOX_EMPTY;
    }
    sc_handle fresh;
    if (!sc_handle_alloc(a, t, &fresh))
        return SC_MBOX_NOSLOT;
    *out = fresh;
    *value = slot->value;
    atomic_store_explicit(&slot->seq, 2 * (head + m->cap), memory_order_release);
    m->head = head + 1;
    return SC_MBOX_OK;
}
