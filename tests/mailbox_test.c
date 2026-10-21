#include "safec/arena.h"
#include "safec/handle.h"
#include "safec/mailbox.h"

/* Each thread owns an arena and a handle table, as the mailbox requires. The
 * mailbox itself lives in the receiver's arena. */
#define SIDE_STORE 512
#include "side.h"
#include <pthread.h>

static sc_mbox_status send(side *s, sc_mbox *m, sc_handle h, size_t v) {
    return sc_mbox_send(m, &s->arena, s->table, h, v);
}

static sc_mbox_status recv(side *r, sc_mbox *m, sc_handle *h, size_t *v) {
    return sc_mbox_recv(m, &r->arena, r->table, h, v);
}

static void test_new(void) {
    side r;
    side_init(&r, 1);
    sc_mbox m = {0};
    /* Zero, non-power-of-two and oversized capacities refuse and leave it untouched. */
    assert(!sc_mbox_new(&m, &r.arena, 0));
    assert(!sc_mbox_new(&m, &r.arena, 3));
    assert(!sc_mbox_new(&m, &r.arena, SIZE_MAX));
    assert(!sc_mbox_new(&m, &r.arena, (SIZE_MAX >> 1) + 1));
    assert(!sc_mbox_new(&m, &r.arena, (size_t)1 << 40)); /* passes the guard, no room */
    assert(m.slots == NULL && m.cap == 0);
    /* 512 bytes minus the 8-byte table leaves 504 = 31 slots of 16: capacity 32 is too big. */
    size_t used = r.arena.used;
    assert(!sc_mbox_new(&m, &r.arena, 32));
    assert(r.arena.used == used && m.slots == NULL);
    assert(sc_mbox_new(&m, &r.arena, 16));
    assert(m.cap == 16 && m.slots != NULL);
}

/* Send: the message moves into the mailbox and the sender's copy goes stale. */
static void test_send_invalidates_sender(void) {
    side s, r;
    side_init(&s, 2);
    side_init(&r, 2);
    sc_mbox m;
    assert(sc_mbox_new(&m, &r.arena, 2));
    sc_handle h = take(&s);
    sc_handle copy = h;
    assert(send(&s, &m, h, 42) == SC_MBOX_OK);
    assert(!live(&s, h) && !live(&s, copy));
    sc_handle again = take(&s);
    assert(again.index == h.index && again.generation == h.generation + 1);
    assert(!live(&s, h) && live(&s, again));
}

/* Stale: a handle sent twice, forged, or freed is refused and queues nothing. */
static void test_stale(void) {
    side s, r;
    side_init(&s, 2);
    side_init(&r, 2);
    sc_mbox m;
    assert(sc_mbox_new(&m, &r.arena, 4));
    sc_handle h = take(&s);
    assert(send(&s, &m, h, 1) == SC_MBOX_OK);
    assert(send(&s, &m, h, 2) == SC_MBOX_STALE);
    sc_handle forged = {1, 9, s.table.epoch, 0};
    assert(send(&s, &m, forged, 3) == SC_MBOX_STALE);
    sc_handle freed = take(&s);
    assert(sc_handle_free(&s.arena, s.table, freed));
    assert(send(&s, &m, freed, 4) == SC_MBOX_STALE);
    sc_handle got;
    size_t v;
    assert(recv(&r, &m, &got, &v) == SC_MBOX_OK && v == 1);
    assert(recv(&r, &m, &got, &v) == SC_MBOX_EMPTY);
}

/* Empty: recv refuses on an open mailbox and leaves its outputs alone. Recv: the
 * oldest message arrives with a live handle. */
static void test_empty_and_fifo(void) {
    side s, r;
    side_init(&s, 4);
    side_init(&r, 4);
    sc_mbox m;
    assert(sc_mbox_new(&m, &r.arena, 4));
    sc_handle got = {7, 7, 7, 7};
    size_t v = 99;
    assert(recv(&r, &m, &got, &v) == SC_MBOX_EMPTY);
    assert(got.index == 7 && v == 99);
    for (size_t i = 0; i < 4; i++)
        assert(send(&s, &m, take(&s), 100 + i) == SC_MBOX_OK);
    for (size_t i = 0; i < 4; i++) {
        assert(recv(&r, &m, &got, &v) == SC_MBOX_OK);
        assert(v == 100 + i);
        assert(live(&r, got));
        assert(sc_handle_free(&r.arena, r.table, got));
    }
    assert(recv(&r, &m, &got, &v) == SC_MBOX_EMPTY);
}

/* Full: the send is refused and the sender keeps its handle; a receive makes
 * room, and the ring wraps. */
static void test_full(void) {
    side s, r;
    side_init(&s, 3);
    side_init(&r, 3);
    sc_mbox m;
    assert(sc_mbox_new(&m, &r.arena, 2));
    assert(send(&s, &m, take(&s), 1) == SC_MBOX_OK);
    assert(send(&s, &m, take(&s), 2) == SC_MBOX_OK);
    sc_handle kept = take(&s);
    assert(send(&s, &m, kept, 3) == SC_MBOX_FULL);
    assert(live(&s, kept));
    sc_handle got;
    size_t v;
    assert(recv(&r, &m, &got, &v) == SC_MBOX_OK && v == 1);
    assert(send(&s, &m, kept, 3) == SC_MBOX_OK && !live(&s, kept));
    for (size_t i = 0; i < 10; i++) {
        assert(sc_handle_free(&r.arena, r.table, got));
        assert(recv(&r, &m, &got, &v) == SC_MBOX_OK && v == 2 + i);
        assert(send(&s, &m, take(&s), 4 + i) == SC_MBOX_OK);
    }
}

/* No free receiver slot: recv refuses and the message stays queued. */
static void test_no_slot(void) {
    side s, r;
    side_init(&s, 2);
    side_init(&r, 1);
    sc_mbox m;
    assert(sc_mbox_new(&m, &r.arena, 2));
    assert(send(&s, &m, take(&s), 1) == SC_MBOX_OK);
    assert(send(&s, &m, take(&s), 2) == SC_MBOX_OK);
    sc_handle first, second = {5, 5, 5, 5};
    size_t v = 0;
    assert(recv(&r, &m, &first, &v) == SC_MBOX_OK && v == 1);
    v = 55;
    assert(recv(&r, &m, &second, &v) == SC_MBOX_NOSLOT);
    assert(second.index == 5 && v == 55);
    assert(sc_handle_free(&r.arena, r.table, first));
    assert(recv(&r, &m, &second, &v) == SC_MBOX_OK && v == 2);
}

/* Close: no further send; accepted messages drain; then recv reports closed. */
static void test_close_and_drain(void) {
    side s, r;
    side_init(&s, 3);
    side_init(&r, 3);
    sc_mbox m;
    assert(sc_mbox_new(&m, &r.arena, 4));
    assert(send(&s, &m, take(&s), 1) == SC_MBOX_OK);
    assert(send(&s, &m, take(&s), 2) == SC_MBOX_OK);
    sc_handle unsent = take(&s);
    sc_mbox_close(&m);
    sc_mbox_close(&m); /* idempotent */
    assert(send(&s, &m, unsent, 3) == SC_MBOX_CLOSED);
    assert(live(&s, unsent)); /* a refused send leaves the sender in possession */
    sc_handle forged = {2, 9, s.table.epoch, 0};
    assert(send(&s, &m, forged, 3) == SC_MBOX_STALE);
    sc_handle got;
    size_t v;
    assert(recv(&r, &m, &got, &v) == SC_MBOX_OK && v == 1);
    assert(sc_handle_free(&r.arena, r.table, got));
    assert(recv(&r, &m, &got, &v) == SC_MBOX_OK && v == 2);
    assert(sc_handle_free(&r.arena, r.table, got));
    assert(recv(&r, &m, &got, &v) == SC_MBOX_CLOSED);
    assert(recv(&r, &m, &got, &v) == SC_MBOX_CLOSED);
    assert(sc_handle_free(&s.arena, s.table, unsent));
    assert(send(&s, &m, take(&s), 4) == SC_MBOX_CLOSED);
}

/* Closing a full mailbox still drains it, and senders to a closed full mailbox
 * hear CLOSED, not FULL. */
static void test_close_full(void) {
    side s, r;
    side_init(&s, 2);
    side_init(&r, 2);
    sc_mbox m;
    assert(sc_mbox_new(&m, &r.arena, 1));
    assert(send(&s, &m, take(&s), 8) == SC_MBOX_OK);
    sc_mbox_close(&m);
    assert(send(&s, &m, take(&s), 9) == SC_MBOX_CLOSED);
    sc_handle got;
    size_t v;
    assert(recv(&r, &m, &got, &v) == SC_MBOX_OK && v == 8);
    assert(recv(&r, &m, &got, &v) == SC_MBOX_CLOSED);
}

/* A receive on a closed mailbox with a claimed but unpublished place is EMPTY,
 * not CLOSED: the accepted message is still coming. */
static void test_close_with_claim_in_flight(void) {
    side r;
    side_init(&r, 1);
    sc_mbox m;
    assert(sc_mbox_new(&m, &r.arena, 2));
    /* Play a sender that has claimed place 0 but not yet written it. */
    atomic_store(&m.tail, 2);
    sc_mbox_close(&m);
    sc_handle got;
    size_t v;
    assert(recv(&r, &m, &got, &v) == SC_MBOX_EMPTY);
    m.slots[0].value = 77;
    atomic_store(&m.slots[0].seq, 1); /* published position 0 */
    assert(recv(&r, &m, &got, &v) == SC_MBOX_OK && v == 77);
    assert(recv(&r, &m, &got, &v) == SC_MBOX_CLOSED);
}

#define SENDERS 4u
#define PER_SENDER 5000u
#define STRIDE (PER_SENDER * 4)

typedef struct {
    side side;
    sc_mbox *m;
    size_t id;
    size_t sent; /* messages the mailbox accepted */
    bool until_closed;
} sender_args;

static void *sender(void *p) {
    sender_args *a = p;
    for (size_t i = 0; a->until_closed || i < PER_SENDER;) {
        sc_handle h;
        if (!sc_handle_alloc(&a->side.arena, a->side.table, &h))
            continue;
        sc_mbox_status st;
        while ((st = sc_mbox_send(a->m, &a->side.arena, a->side.table, h, a->id * STRIDE + i)) ==
               SC_MBOX_FULL) {
        }
        if (st == SC_MBOX_CLOSED) {
            /* A refused send leaves the sender in possession of its handle. */
            assert(sc_handle_get(&a->side.arena, a->side.table, h).some);
            assert(sc_handle_free(&a->side.arena, a->side.table, h));
            return NULL;
        }
        assert(st == SC_MBOX_OK);
        assert(!sc_handle_get(&a->side.arena, a->side.table, h).some);
        a->sent++;
        i++;
    }
    return NULL;
}

static void start_senders(sender_args *args, pthread_t *t, sc_mbox *m, bool until_closed) {
    for (size_t i = 0; i < SENDERS; i++) {
        side_init(&args[i].side, 2);
        args[i].m = m;
        args[i].id = i;
        args[i].sent = 0;
        args[i].until_closed = until_closed;
        assert(pthread_create(&t[i], NULL, sender, &args[i]) == 0);
    }
}

/* Four senders, one receiver: every message arrives once, each sender's
 * messages in order, and ThreadSanitizer (make tsan) sees no race. */
static void test_threads(void) {
    static sender_args args[SENDERS];
    side r;
    side_init(&r, 4);
    sc_mbox m;
    assert(sc_mbox_new(&m, &r.arena, 8));
    pthread_t t[SENDERS];
    start_senders(args, t, &m, false);
    size_t next[SENDERS] = {0};
    size_t total = 0;
    while (total < SENDERS * PER_SENDER) {
        sc_handle got;
        size_t v;
        sc_mbox_status st = recv(&r, &m, &got, &v);
        if (st != SC_MBOX_OK) {
            assert(st == SC_MBOX_EMPTY);
            continue;
        }
        size_t who = v / STRIDE;
        assert(who < SENDERS && v % STRIDE == next[who]++);
        assert(live(&r, got));
        assert(sc_handle_free(&r.arena, r.table, got));
        total++;
    }
    for (size_t i = 0; i < SENDERS; i++)
        assert(pthread_join(t[i], NULL) == 0);
    sc_handle got;
    size_t v;
    assert(recv(&r, &m, &got, &v) == SC_MBOX_EMPTY);
    for (size_t i = 0; i < SENDERS; i++)
        assert(next[i] == PER_SENDER && args[i].sent == PER_SENDER);
}

/* Senders race with close: every send the mailbox accepted is received, and
 * every send it refused leaves the handle with the sender. */
static void test_close_race(void) {
    static sender_args args[SENDERS];
    side r;
    side_init(&r, 4);
    sc_mbox m;
    assert(sc_mbox_new(&m, &r.arena, 4));
    pthread_t t[SENDERS];
    start_senders(args, t, &m, true);
    size_t received = 0, next[SENDERS] = {0};
    bool closed = false;
    for (;;) {
        sc_handle got;
        size_t v;
        sc_mbox_status st = recv(&r, &m, &got, &v);
        if (st == SC_MBOX_CLOSED)
            break;
        if (st == SC_MBOX_OK) {
            size_t who = v / STRIDE;
            assert(who < SENDERS && v % STRIDE == next[who]++);
            assert(sc_handle_free(&r.arena, r.table, got));
            received++;
        } else {
            assert(st == SC_MBOX_EMPTY);
        }
        if (!closed && received >= 1000) {
            sc_mbox_close(&m);
            closed = true;
        }
    }
    for (size_t i = 0; i < SENDERS; i++)
        assert(pthread_join(t[i], NULL) == 0);
    size_t sent = 0;
    for (size_t i = 0; i < SENDERS; i++)
        sent += args[i].sent;
    assert(sent == received);
}

int main(void) {
    test_new();
    test_send_invalidates_sender();
    test_stale();
    test_empty_and_fifo();
    test_full();
    test_no_slot();
    test_close_and_drain();
    test_close_full();
    test_close_with_claim_in_flight();
    test_threads();
    test_close_race();
    return 0;
}
