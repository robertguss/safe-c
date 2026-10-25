#include "safec/arena.h"
#include "safec/chan.h"
#include "safec/handle.h"

bool sc_chan_new(sc_chan *c, sc_arena *a, size_t cap) {
    if (cap == 0 || cap > SIZE_MAX / sizeof(size_t) - 1)
        return false;
    size_t entries = cap + 1;
    sc_slice s = sc_arena_alloc(a, entries * sizeof(size_t));
    if (s.len == 0)
        return false;
    c->ring = (size_t *)(void *)s.ptr;
    c->entries = entries;
    atomic_init(&c->head, 0);
    atomic_init(&c->tail, 0);
    atomic_init(&c->closed, false);
    return true;
}

sc_chan_status sc_chan_send(sc_chan *c, const sc_arena *a, sc_handle_table t, sc_handle h,
                            size_t value) {
    if (!sc_handle_get(a, t, h).some)
        return SC_CHAN_STALE;
    if (atomic_load_explicit(&c->closed, memory_order_acquire))
        return SC_CHAN_CLOSED;
    size_t tail = atomic_load_explicit(&c->tail, memory_order_relaxed);
    size_t next = tail + 1 == c->entries ? 0 : tail + 1;
    if (next == atomic_load_explicit(&c->head, memory_order_acquire))
        return SC_CHAN_FULL;
    /* The handle was just checked live, so freeing it cannot fail. */
    (void)sc_handle_free(a, t, h);
    c->ring[tail] = value;
    atomic_store_explicit(&c->tail, next, memory_order_release);
    return SC_CHAN_OK;
}

void sc_chan_close(sc_chan *c) {
    atomic_store_explicit(&c->closed, true, memory_order_release);
}

sc_chan_status sc_chan_recv(sc_chan *c, const sc_arena *a, sc_handle_table t, sc_handle *out,
                            size_t *value) {
    size_t head = atomic_load_explicit(&c->head, memory_order_relaxed);
    if (head == atomic_load_explicit(&c->tail, memory_order_acquire)) {
        /* Read closed first, then tail again: a send that finished before the
         * close is visible by now, so closed-and-empty is final. */
        if (!atomic_load_explicit(&c->closed, memory_order_acquire))
            return SC_CHAN_EMPTY;
        if (head == atomic_load_explicit(&c->tail, memory_order_acquire))
            return SC_CHAN_CLOSED;
    }
    sc_handle fresh;
    if (!sc_handle_alloc(a, t, &fresh))
        return SC_CHAN_NOSLOT;
    *out = fresh;
    *value = c->ring[head];
    atomic_store_explicit(&c->head, head + 1 == c->entries ? 0 : head + 1, memory_order_release);
    return SC_CHAN_OK;
}
