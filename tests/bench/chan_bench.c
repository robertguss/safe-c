/* Times messages per second through sc_chan with one producer and one consumer
 * thread. Built by `make bench` with -O2 and no sanitizers; the number is
 * recorded, not gated. */
#define _POSIX_C_SOURCE 199309L
#include "safec/arena.h"
#include "safec/chan.h"
#include "safec/handle.h"
#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <time.h>

#define MESSAGES 5000000u
#define QUEUE 1024

typedef struct {
    alignas(8) uint8_t store[16384];
    sc_arena arena;
    sc_handle_table table;
} side;

typedef struct {
    side *s;
    sc_chan *c;
} producer_args;

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static void *producer(void *p) {
    producer_args *a = p;
    for (size_t i = 0; i < MESSAGES;) {
        sc_handle h;
        if (!sc_handle_alloc(&a->s->arena, a->s->table, &h))
            continue;
        while (sc_chan_send(a->c, &a->s->arena, a->s->table, h, i) == SC_CHAN_FULL) {
        }
        i++;
    }
    sc_chan_close(a->c);
    return NULL;
}

int main(void) {
    static side s, r;
    sc_arena_init(&s.arena, s.store, sizeof s.store);
    sc_arena_init(&r.arena, r.store, sizeof r.store);
    s.table = sc_handle_table_new(&s.arena, QUEUE + 2);
    r.table = sc_handle_table_new(&r.arena, QUEUE + 2);
    static sc_chan c;
    if (!sc_chan_new(&c, &s.arena, QUEUE))
        return 1;
    producer_args args = {&s, &c};
    pthread_t t;
    double start = now_ns();
    if (pthread_create(&t, NULL, producer, &args) != 0)
        return 1;
    size_t got = 0;
    for (;;) {
        sc_handle h;
        size_t v;
        sc_chan_status st = sc_chan_recv(&c, &r.arena, r.table, &h, &v);
        if (st == SC_CHAN_CLOSED)
            break;
        if (st == SC_CHAN_OK) {
            got++;
            sc_handle_free(&r.arena, r.table, h);
        }
    }
    double elapsed = now_ns() - start;
    pthread_join(t, NULL);
    if (got != MESSAGES)
        return 1;
    printf("sc_chan: %.2f million messages/s, %.1f ns/message (%u messages, queue %d)\n",
           (double)got / elapsed * 1e3, elapsed / (double)got, MESSAGES, QUEUE);
    return 0;
}
