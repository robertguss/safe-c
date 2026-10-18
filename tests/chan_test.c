#include "safec/arena.h"
#include "safec/chan.h"
#include "safec/handle.h"
#include <assert.h>
#include <pthread.h>
#include <stdalign.h>
#include <stdint.h>

/* Each side owns an arena and a handle table, as the channel requires. The
 * channel ring lives in the sender's arena. */
typedef struct {
    alignas(8) uint8_t store[256];
    sc_arena arena;
    sc_handle_table table;
} side;

static void side_init(side *s, size_t slots) {
    sc_arena_init(&s->arena, s->store, sizeof s->store);
    s->table = sc_handle_table_new(&s->arena, slots);
    assert(s->table.cap == slots);
}

static sc_handle take(side *s) {
    sc_handle h;
    assert(sc_handle_alloc(&s->arena, s->table, &h));
    return h;
}

static bool live(side *s, sc_handle h) {
    return sc_handle_get(&s->arena, s->table, h).some;
}

static sc_chan_status send(side *s, sc_chan *c, sc_handle h, size_t v) {
    return sc_chan_send(c, &s->arena, s->table, h, v);
}

static sc_chan_status recv(side *r, sc_chan *c, sc_handle *h, size_t *v) {
    return sc_chan_recv(c, &r->arena, r->table, h, v);
}

static void test_new(void) {
    side s;
    side_init(&s, 1);
    sc_chan c = {0};
    /* Zero and oversized capacities refuse and leave the channel untouched. */
    assert(!sc_chan_new(&c, &s.arena, 0));
    assert(!sc_chan_new(&c, &s.arena, SIZE_MAX));
    assert(!sc_chan_new(&c, &s.arena, SIZE_MAX / sizeof(size_t)));
    assert(!sc_chan_new(&c, &s.arena, SIZE_MAX / sizeof(size_t) - 1)); /* passes the guard, no room */
    assert(c.ring == NULL && c.entries == 0);
    /* 256 bytes minus the 8-byte table leaves 248 = 31 entries = capacity 30. */
    size_t used = s.arena.used;
    assert(!sc_chan_new(&c, &s.arena, 31));
    assert(s.arena.used == used && c.ring == NULL);
    assert(sc_chan_new(&c, &s.arena, 30));
    assert(c.entries == 31 && c.ring != NULL);
}

/* Alloc: the sender takes a fresh message and owns it. SenderUse: the handle
 * resolves until it is sent. */
static void test_alloc_and_sender_use(void) {
    side s;
    side_init(&s, 2);
    sc_handle h = take(&s);
    assert(live(&s, h));
    sc_option_size o = sc_handle_get(&s.arena, s.table, h);
    assert(o.some && o.value == h.index);
}

/* Send: the message moves into the queue and the sender's copy goes stale. */
static void test_send_invalidates_sender(void) {
    side s, r;
    side_init(&s, 2);
    side_init(&r, 2);
    sc_chan c;
    assert(sc_chan_new(&c, &s.arena, 2));
    sc_handle h = take(&s);
    sc_handle copy = h;
    assert(send(&s, &c, h, 42) == SC_CHAN_OK);
    assert(!live(&s, h) && !live(&s, copy));
    assert(sc_handle_get(&s.arena, s.table, h).value == 0);
    /* The slot is reusable but a new handle differs by generation. */
    sc_handle again = take(&s);
    assert(again.index == h.index && again.generation == h.generation + 1);
    assert(!live(&s, h) && live(&s, again));
}

/* Use after send: a second send of the same handle is refused, queues nothing. */
static void test_use_after_send(void) {
    side s, r;
    side_init(&s, 2);
    side_init(&r, 2);
    sc_chan c;
    assert(sc_chan_new(&c, &s.arena, 3));
    sc_handle h = take(&s);
    assert(send(&s, &c, h, 1) == SC_CHAN_OK);
    assert(send(&s, &c, h, 2) == SC_CHAN_STALE);
    sc_handle got;
    size_t v;
    assert(recv(&r, &c, &got, &v) == SC_CHAN_OK && v == 1);
    assert(recv(&r, &c, &got, &v) == SC_CHAN_EMPTY);
    /* A forged handle is stale too. */
    sc_handle forged = {1, 9, s.table.epoch, 0};
    assert(send(&s, &c, forged, 3) == SC_CHAN_STALE);
}

/* Recv: the oldest message moves to the receiver, who gets a live handle. */
static void test_recv_and_fifo(void) {
    side s, r;
    side_init(&s, 3);
    side_init(&r, 3);
    sc_chan c;
    assert(sc_chan_new(&c, &s.arena, 3));
    sc_handle got = {7, 7, 7, 7};
    size_t v = 99;
    assert(recv(&r, &c, &got, &v) == SC_CHAN_EMPTY);
    assert(got.index == 7 && v == 99); /* untouched on failure */
    for (size_t i = 0; i < 3; i++)
        assert(send(&s, &c, take(&s), 100 + i) == SC_CHAN_OK);
    for (size_t i = 0; i < 3; i++) {
        assert(recv(&r, &c, &got, &v) == SC_CHAN_OK);
        assert(v == 100 + i);
        assert(live(&r, got));
    }
    assert(recv(&r, &c, &got, &v) == SC_CHAN_EMPTY);
}

/* Free: the receiver releases a message; the sender frees one it never sent. */
static void test_free(void) {
    side s, r;
    side_init(&s, 1);
    side_init(&r, 1);
    sc_chan c;
    assert(sc_chan_new(&c, &s.arena, 1));
    sc_handle h = take(&s);
    assert(send(&s, &c, h, 5) == SC_CHAN_OK);
    sc_handle got;
    size_t v;
    assert(recv(&r, &c, &got, &v) == SC_CHAN_OK);
    assert(sc_handle_free(&r.arena, r.table, got));
    assert(!live(&r, got));
    /* The receiver's slot is free again, so the next message fits. */
    assert(send(&s, &c, take(&s), 6) == SC_CHAN_OK);
    assert(recv(&r, &c, &got, &v) == SC_CHAN_OK && v == 6);
    /* An unsent message is the sender's to free. */
    sc_handle unsent = take(&s);
    assert(sc_handle_free(&s.arena, s.table, unsent));
    assert(send(&s, &c, unsent, 7) == SC_CHAN_STALE);
}

/* Full queue: the send is refused and the sender keeps its handle. */
static void test_full(void) {
    side s, r;
    side_init(&s, 3);
    side_init(&r, 3);
    sc_chan c;
    assert(sc_chan_new(&c, &s.arena, 2));
    assert(send(&s, &c, take(&s), 1) == SC_CHAN_OK);
    assert(send(&s, &c, take(&s), 2) == SC_CHAN_OK);
    sc_handle kept = take(&s);
    assert(send(&s, &c, kept, 3) == SC_CHAN_FULL);
    assert(live(&s, kept));
    sc_handle got;
    size_t v;
    assert(recv(&r, &c, &got, &v) == SC_CHAN_OK && v == 1);
    assert(send(&s, &c, kept, 3) == SC_CHAN_OK && !live(&s, kept));
    /* The ring wraps: drain and refill past the end of the buffer. */
    for (size_t i = 0; i < 10; i++) {
        assert(recv(&r, &c, &got, &v) == SC_CHAN_OK);
        assert(sc_handle_free(&r.arena, r.table, got));
        assert(send(&s, &c, take(&s), 10 + i) == SC_CHAN_OK);
    }
}

/* Receiver's table full: recv refuses and the message stays queued. */
static void test_no_slot(void) {
    side s, r;
    side_init(&s, 2);
    side_init(&r, 1);
    sc_chan c;
    assert(sc_chan_new(&c, &s.arena, 2));
    assert(send(&s, &c, take(&s), 1) == SC_CHAN_OK);
    assert(send(&s, &c, take(&s), 2) == SC_CHAN_OK);
    sc_handle first, second = {5, 5, 5, 5};
    size_t v = 0;
    assert(recv(&r, &c, &first, &v) == SC_CHAN_OK && v == 1);
    v = 55;
    assert(recv(&r, &c, &second, &v) == SC_CHAN_NOSLOT);
    assert(second.index == 5 && v == 55);
    assert(sc_handle_free(&r.arena, r.table, first));
    assert(recv(&r, &c, &second, &v) == SC_CHAN_OK && v == 2);
}

/* Close: no further send; queued messages drain; then recv reports closed.
 * Terminated: closed, drained, every message released. */
static void test_close_and_terminate(void) {
    side s, r;
    side_init(&s, 3);
    side_init(&r, 3);
    sc_chan c;
    assert(sc_chan_new(&c, &s.arena, 3));
    assert(send(&s, &c, take(&s), 1) == SC_CHAN_OK);
    assert(send(&s, &c, take(&s), 2) == SC_CHAN_OK);
    sc_handle unsent = take(&s);
    sc_chan_close(&c);
    sc_chan_close(&c); /* idempotent */
    assert(send(&s, &c, unsent, 3) == SC_CHAN_CLOSED);
    assert(live(&s, unsent)); /* a refused send leaves the sender in possession */
    sc_handle forged = {2, 9, s.table.epoch, 0};
    assert(send(&s, &c, forged, 3) == SC_CHAN_STALE);
    sc_handle got;
    size_t v;
    assert(recv(&r, &c, &got, &v) == SC_CHAN_OK && v == 1);
    assert(sc_handle_free(&r.arena, r.table, got));
    assert(recv(&r, &c, &got, &v) == SC_CHAN_OK && v == 2);
    assert(sc_handle_free(&r.arena, r.table, got));
    assert(recv(&r, &c, &got, &v) == SC_CHAN_CLOSED);
    assert(recv(&r, &c, &got, &v) == SC_CHAN_CLOSED);
    assert(sc_handle_free(&s.arena, s.table, unsent));
    /* A closed channel stays closed even if the queue has room. */
    assert(send(&s, &c, take(&s), 4) == SC_CHAN_CLOSED);
}

/* Closing a full queue still drains it. */
static void test_close_full(void) {
    side s, r;
    side_init(&s, 2);
    side_init(&r, 2);
    sc_chan c;
    assert(sc_chan_new(&c, &s.arena, 1));
    assert(send(&s, &c, take(&s), 8) == SC_CHAN_OK);
    sc_chan_close(&c);
    sc_handle got;
    size_t v;
    assert(recv(&r, &c, &got, &v) == SC_CHAN_OK && v == 8);
    assert(recv(&r, &c, &got, &v) == SC_CHAN_CLOSED);
}

#define MESSAGES 20000u

typedef struct {
    side *s;
    sc_chan *c;
} producer_args;

static void *producer(void *p) {
    producer_args *a = p;
    for (size_t i = 0; i < MESSAGES;) {
        sc_handle h;
        if (!sc_handle_alloc(&a->s->arena, a->s->table, &h))
            continue;
        sc_chan_status st;
        while ((st = sc_chan_send(a->c, &a->s->arena, a->s->table, h, i)) == SC_CHAN_FULL) {
        }
        assert(st == SC_CHAN_OK);
        assert(!sc_handle_get(&a->s->arena, a->s->table, h).some);
        i++;
    }
    sc_chan_close(a->c);
    return NULL;
}

/* Two threads, one producer and one consumer: every message arrives once, in
 * order, and ThreadSanitizer (make tsan) sees no race. */
static void test_threads(void) {
    side s, r;
    side_init(&s, 4);
    side_init(&r, 4);
    sc_chan c;
    assert(sc_chan_new(&c, &s.arena, 8));
    producer_args args = {&s, &c};
    pthread_t t;
    assert(pthread_create(&t, NULL, producer, &args) == 0);
    size_t expect = 0;
    for (;;) {
        sc_handle got;
        size_t v;
        sc_chan_status st = recv(&r, &c, &got, &v);
        if (st == SC_CHAN_CLOSED)
            break;
        if (st != SC_CHAN_OK) {
            assert(st == SC_CHAN_EMPTY);
            continue;
        }
        assert(v == expect++);
        assert(live(&r, got));
        assert(sc_handle_free(&r.arena, r.table, got));
    }
    assert(pthread_join(t, NULL) == 0);
    assert(expect == MESSAGES);
}

int main(void) {
    test_new();
    test_alloc_and_sender_use();
    test_send_invalidates_sender();
    test_use_after_send();
    test_recv_and_fifo();
    test_free();
    test_full();
    test_no_slot();
    test_close_and_terminate();
    test_close_full();
    test_threads();
    return 0;
}
