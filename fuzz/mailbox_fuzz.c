#include "safec/arena.h"
#include "safec/handle.h"
#include "safec/mailbox.h"
#include <assert.h>
#include <stdalign.h>

#define SENDERS 3
#define CAP_MAX 4
#define SLOTS 4
#define KEPT 8

/* One sender: its own arena, handle table, and the handles it holds or has sent. */
typedef struct {
    alignas(8) uint8_t store[64];
    sc_arena arena;
    sc_handle_table table;
    sc_handle held[KEPT]; /* live, unsent */
    size_t nheld;
    sc_handle sent[KEPT]; /* once held, for use-after-send */
    size_t nsent;
} sender;

/* Reference model: a plain FIFO of payloads and a closed flag, compared with
 * the mailbox after every operation. */
typedef struct {
    size_t q[CAP_MAX];
    size_t len;
    size_t cap;
    bool closed;
} model;

static void pop(model *m) {
    for (size_t i = 1; i < m->len; i++)
        m->q[i - 1] = m->q[i];
    m->len--;
}

static void drop(sender *s, size_t i) {
    s->held[i] = s->held[--s->nheld];
}

static void do_send(sender *s, sc_mbox *mb, model *m, uint8_t arg, size_t *next_value) {
    sc_handle h;
    if (s->nheld > 0 && arg % 3 != 0)
        h = s->held[arg % s->nheld];
    else if (s->nsent > 0)
        h = s->sent[arg % s->nsent];
    else
        h = (sc_handle){arg % SLOTS, arg, s->arena.epoch, 0};
    bool is_live = sc_handle_get(&s->arena, s->table, h).some;
    sc_mbox_status got = sc_mbox_send(mb, &s->arena, s->table, h, *next_value);
    if (!is_live)
        assert(got == SC_MBOX_STALE);
    else if (m->closed)
        assert(got == SC_MBOX_CLOSED);
    else if (m->len == m->cap)
        assert(got == SC_MBOX_FULL);
    else
        assert(got == SC_MBOX_OK);
    if (got == SC_MBOX_OK) {
        m->q[m->len++] = (*next_value)++;
        assert(!sc_handle_get(&s->arena, s->table, h).some); /* the sender's copy is stale */
        if (s->nsent < KEPT)
            s->sent[s->nsent++] = h;
        for (size_t i = 0; i < s->nheld; i++) {
            if (s->held[i].index == h.index) {
                drop(s, i);
                break;
            }
        }
    } else if (is_live) {
        assert(sc_handle_get(&s->arena, s->table, h).some); /* refused: still the sender's */
    }
}

/* Everything one run touches: the mailbox, the receiver's side, the senders, the
 * reference model and the receiver's live handles. */
typedef struct {
    alignas(8) uint8_t rstore[256];
    sc_arena ra;
    sc_handle_table rt;
    sc_mbox mb;
    sender senders[SENDERS];
    model m;
    sc_handle recvd[KEPT]; /* the receiver's live handles */
    size_t nrecvd;
    size_t next_value;
} world;

static void do_recv(world *w) {
    sc_handle out = {91, 91, 91, 91};
    size_t v = 777;
    sc_mbox_status got = sc_mbox_recv(&w->mb, &w->ra, w->rt, &out, &v);
    if (w->m.len == 0)
        assert(got == (w->m.closed ? SC_MBOX_CLOSED : SC_MBOX_EMPTY));
    else if (got == SC_MBOX_NOSLOT)
        assert(w->nrecvd == SLOTS);
    else
        assert(got == SC_MBOX_OK && w->nrecvd < SLOTS);
    if (got != SC_MBOX_OK) {
        assert(out.index == 91 && v == 777);
        return;
    }
    assert(v == w->m.q[0]); /* FIFO across all senders */
    pop(&w->m);
    assert(sc_handle_get(&w->ra, w->rt, out).some);
    if (w->nrecvd < KEPT)
        w->recvd[w->nrecvd++] = out;
}

static void do_free(world *w, sender *s, uint8_t op, uint8_t arg) {
    if (op == 3 && w->nrecvd > 0) { /* Free (receiver) */
        size_t i = arg % w->nrecvd;
        assert(sc_handle_free(&w->ra, w->rt, w->recvd[i]));
        assert(!sc_handle_get(&w->ra, w->rt, w->recvd[i]).some);
        w->recvd[i] = w->recvd[--w->nrecvd];
    } else if (op == 4 && s->nheld > 0) { /* Free (sender, unsent) */
        size_t i = arg % s->nheld;
        assert(sc_handle_free(&s->arena, s->table, s->held[i]));
        drop(s, i);
    }
}

static void step(world *w, uint8_t op, uint8_t arg, sender *s) {
    if (op == 0 && s->nheld < KEPT) { /* Alloc */
        sc_handle h;
        if (sc_handle_alloc(&s->arena, s->table, &h))
            s->held[s->nheld++] = h;
    } else if (op == 1) { /* Send, from any sender */
        do_send(s, &w->mb, &w->m, arg, &w->next_value);
    } else if (op == 2) {
        do_recv(w);
    } else if (op == 5) { /* Close */
        sc_mbox_close(&w->mb);
        w->m.closed = true;
    } else {
        do_free(w, s, op, arg);
    }
}

/* Drain: closed or not, accepted messages arrive in order. */
static void drain(world *w) {
    sc_mbox_close(&w->mb);
    w->m.closed = true;
    while (w->m.len > 0) {
        sc_handle out;
        size_t v;
        if (w->nrecvd == SLOTS)
            assert(sc_handle_free(&w->ra, w->rt, w->recvd[--w->nrecvd]));
        assert(sc_mbox_recv(&w->mb, &w->ra, w->rt, &out, &v) == SC_MBOX_OK);
        assert(v == w->m.q[0]);
        pop(&w->m);
        w->recvd[w->nrecvd++] = out;
    }
    sc_handle out;
    size_t v;
    assert(sc_mbox_recv(&w->mb, &w->ra, w->rt, &out, &v) == SC_MBOX_CLOSED);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1)
        return 0;
    world w = {.m = {{0}, 0, 0, false}, .nrecvd = 0, .next_value = 1};
    sc_arena_init(&w.ra, w.rstore, sizeof w.rstore);
    w.rt = sc_handle_table_new(&w.ra, SLOTS);
    assert(w.rt.cap == SLOTS);
    for (size_t i = 0; i < SENDERS; i++) {
        sender *s = &w.senders[i];
        s->nheld = s->nsent = 0;
        sc_arena_init(&s->arena, s->store, sizeof s->store);
        s->table = sc_handle_table_new(&s->arena, SLOTS);
        assert(s->table.cap == SLOTS);
    }
    size_t want = data[0] % (CAP_MAX + 1); /* 0 and 3 are refused: capacity is a power of two */
    bool made = sc_mbox_new(&w.mb, &w.ra, want);
    assert(made == (want == 1 || want == 2 || want == 4));
    if (!made)
        return 0;
    w.m.cap = want;
    for (size_t p = 1; p + 2 < size; p += 3)
        step(&w, data[p] % 6, data[p + 1], &w.senders[data[p + 2] % SENDERS]);
    drain(&w);
    return 0;
}
