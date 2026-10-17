/* Times sc_handle_get on live and stale handles. Built by `make bench` with
 * -O2 and no sanitizers; the numbers are recorded, not gated. */
#define _POSIX_C_SOURCE 199309L
#include "safec/arena.h"
#include "safec/handle.h"
#include <stdalign.h>
#include <stdio.h>
#include <time.h>

#define CAP 1024
#define ROUNDS 20000

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static double time_gets(const sc_arena *a, sc_handle_table t, const sc_handle *hs, size_t *hits) {
    volatile size_t sink = 0;
    double start = now_ns();
    for (size_t r = 0; r < ROUNDS; r++) {
        for (size_t i = 0; i < CAP; i++) {
            sc_option_size o = sc_handle_get(a, t, hs[(i * 7 + r) % CAP]);
            sink += o.some ? o.value + 1 : 0;
        }
    }
    double elapsed = now_ns() - start;
    *hits = sink;
    return elapsed / ((double)ROUNDS * CAP);
}

int main(void) {
    static alignas(8) uint8_t storage[3 * CAP + 8];
    sc_arena a;
    sc_arena_init(&a, storage, sizeof storage);
    sc_handle_table t = sc_handle_table_new(&a, CAP);
    static sc_handle live[CAP], stale[CAP];
    for (size_t i = 0; i < CAP; i++) {
        if (!sc_handle_alloc(&a, t, &live[i]))
            return 1;
        stale[i] = live[i];
        stale[i].generation++;
    }
    size_t hits;
    double live_ns = time_gets(&a, t, live, &hits);
    double stale_ns = time_gets(&a, t, stale, &hits);
    printf("sc_handle_get: %.2f ns/call live, %.2f ns/call stale (%d slots, %d calls each)\n",
           live_ns, stale_ns, CAP, ROUNDS * CAP);
    return 0;
}
