/* Tests the thread adapter (adapters/thread.c). Adapters are not in the
 * library, so the test builds the adapter in directly. The include of
 * <pthread.h> below is what makes `make tsan` run this file. */
#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE /* pthread_getattr_np, to read a thread's stack size */
#define SIDE_STORE 4096
#include "../adapters/thread.c"
#include "safec/chan.h"
#include "side.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/time.h>

#define MESSAGES 100000
#define CAP 64

/* One sender, one receiver and a channel between them. The receiver sleeps on
 * `data` and the sender, when the channel is full, sleeps on `space`. */
typedef struct {
    side tx, rx;
    sc_chan c;
    sc_wake data, space;
    size_t received;
} pipeline;

static void producer(void *arg) {
    pipeline *p = arg;
    for (size_t i = 0; i < MESSAGES; i++) {
        sc_handle h = take(&p->tx);
        for (;;) {
            sc_chan_status st = sc_chan_send(&p->c, &p->tx.arena, p->tx.table, h, i);
            if (st == SC_CHAN_OK)
                break;
            assert(st == SC_CHAN_FULL);
            /* Drain first, then retry, so a ring that lands in between is kept. */
            assert(sc_wake_drain(p->space).ok);
            if (sc_chan_send(&p->c, &p->tx.arena, p->tx.table, h, i) == SC_CHAN_OK)
                break;
            assert(sc_wake_wait(p->space, 1000).ok);
        }
        assert(sc_wake_ring(p->data).ok);
    }
    sc_chan_close(&p->c);
    assert(sc_wake_ring(p->data).ok);
}

/* The receiver: waits on the wake, drains it, then empties the channel. */
static void consume(pipeline *p) {
    for (;;) {
        sc_result_size w = sc_wake_wait(p->data, 5000);
        assert(w.ok && w.value == 1); /* never a timeout: every send rings */
        assert(sc_wake_drain(p->data).ok);
        sc_handle h;
        size_t v;
        sc_chan_status st;
        while ((st = sc_chan_recv(&p->c, &p->rx.arena, p->rx.table, &h, &v)) == SC_CHAN_OK) {
            assert(v == p->received);
            p->received++;
            assert(sc_handle_free(&p->rx.arena, p->rx.table, h));
            assert(sc_wake_ring(p->space).ok);
        }
        if (st == SC_CHAN_CLOSED)
            return;
        assert(st == SC_CHAN_EMPTY);
    }
}

static void test_chan_with_wake(void) {
    static pipeline p;
    side_init(&p.tx, CAP);
    side_init(&p.rx, CAP);
    assert(sc_chan_new(&p.c, &p.tx.arena, CAP));
    assert(sc_wake_new(&p.data).ok && sc_wake_new(&p.space).ok);
    sc_thread t;
    assert(sc_thread_start(&t, producer, &p, 256 * 1024).ok);
    consume(&p);
    assert(sc_thread_join(&t).ok);
    assert(p.received == MESSAGES);
    sc_wake_free(&p.data);
    sc_wake_free(&p.space);
}

static void test_wake_coalesces(void) {
    sc_wake w;
    assert(sc_wake_new(&w).ok);
    assert(w.read_fd >= 0 && w.write_fd >= 0 && sc_wake_fd(w) == w.read_fd);
    sc_result_size r = sc_wake_wait(w, 0);
    assert(r.ok && r.value == 0);
    r = sc_wake_drain(w);
    assert(r.ok && r.value == 0);

    /* Two rings, one drain: one wake was pending, and none is left after. */
    assert(sc_wake_ring(w).ok && sc_wake_ring(w).ok);
    r = sc_wake_wait(w, 0);
    assert(r.ok && r.value == 1);
    r = sc_wake_wait(w, 0); /* waiting does not consume */
    assert(r.ok && r.value == 1);
    r = sc_wake_drain(w);
    assert(r.ok && r.value == 1);
    r = sc_wake_wait(w, 0);
    assert(r.ok && r.value == 0);
    /* Not stuck: the next ring wakes again. */
    assert(sc_wake_ring(w).ok);
    r = sc_wake_wait(w, 0);
    assert(r.ok && r.value == 1);

    /* More rings than the pipe holds still succeed, and one drain empties it. */
    for (size_t i = 0; i < 200000; i++)
        assert(sc_wake_ring(w).ok);
    r = sc_wake_drain(w);
    assert(r.ok && r.value == 1);
    r = sc_wake_wait(w, 0);
    assert(r.ok && r.value == 0);

    /* A timeout waits about as long as asked. */
    uint64_t t0 = sc_clock_ms();
    r = sc_wake_wait(w, 30);
    assert(r.ok && r.value == 0);
    assert(sc_clock_ms() - t0 >= 25);

    sc_wake_free(&w);
    assert(w.read_fd == -1 && w.write_fd == -1);
    sc_wake_free(&w); /* idempotent */
    sc_result_size bad = sc_wake_ring(w);
    assert(!bad.ok && bad.err == EBADF && bad.value == 0);
    bad = sc_wake_drain(w);
    assert(!bad.ok && bad.err == EBADF);
    bad = sc_wake_wait(w, 0);
    assert(!bad.ok && bad.err == EBADF);
}

/* With no descriptors left, the pipe fails and both ends read -1. */
static void test_wake_new_fails(void) {
    struct rlimit old, low;
    assert(getrlimit(RLIMIT_NOFILE, &old) == 0);
    low = old;
    low.rlim_cur = 3; /* stdin, stdout and stderr: no room for two more */
    assert(setrlimit(RLIMIT_NOFILE, &low) == 0);
    sc_wake w = {7, 7};
    sc_result_size r = sc_wake_new(&w);
    assert(setrlimit(RLIMIT_NOFILE, &old) == 0);
    assert(!r.ok && r.err == EMFILE && r.value == 0);
    assert(w.read_fd == -1 && w.write_fd == -1);
}

static void on_alarm(int sig) {
    (void)sig;
}

/* A signal that interrupts the wait does not cut the timeout short. */
static void test_wake_wait_eintr(void) {
    sc_wake w;
    assert(sc_wake_new(&w).ok);
    struct sigaction sa = {0}, old;
    sa.sa_handler = on_alarm; /* no SA_RESTART, so poll returns EINTR */
    sigemptyset(&sa.sa_mask);
    assert(sigaction(SIGALRM, &sa, &old) == 0);
    struct itimerval tick = {{0, 0}, {0, 20000}}; /* once, after 20 ms */
    assert(setitimer(ITIMER_REAL, &tick, NULL) == 0);
    uint64_t t0 = sc_clock_ms();
    sc_result_size r = sc_wake_wait(w, 120);
    uint64_t waited = sc_clock_ms() - t0;
    assert(sigaction(SIGALRM, &old, NULL) == 0);
    assert(r.ok && r.value == 0);
    assert(waited >= 115 && waited < 2000);
    sc_wake_free(&w);
}

/* A ring from another thread wakes a waiter that is already asleep. */
static void ring_later(void *arg) {
    sc_sleep_ms(20);
    assert(sc_wake_ring(*(sc_wake *)arg).ok);
}

static void test_wake_across_threads(void) {
    sc_wake w;
    assert(sc_wake_new(&w).ok);
    sc_thread t;
    uint64_t t0 = sc_clock_ms();
    assert(sc_thread_start(&t, ring_later, &w, 0).ok);
    sc_result_size r = sc_wake_wait(w, 5000);
    assert(r.ok && r.value == 1);
    assert(sc_clock_ms() - t0 < 4000);
    assert(sc_thread_join(&t).ok);
    sc_wake_free(&w);
}

typedef struct {
    sc_flag *flag;
    atomic_int *ready;
    atomic_bool *go;
    bool won;
} racer;

static void race(void *arg) {
    racer *r = arg;
    atomic_fetch_add(r->ready, 1);
    while (!atomic_load(r->go)) {
    }
    r->won = sc_flag_set(r->flag);
}

static void test_flag(void) {
    sc_flag f = {0};
    assert(!sc_flag_is_set(&f));
    assert(sc_flag_set(&f));
    assert(sc_flag_is_set(&f));
    assert(!sc_flag_set(&f));
    sc_flag_clear(&f);
    assert(!sc_flag_is_set(&f));
    assert(sc_flag_set(&f));

    /* Two racing setters: exactly one moves the flag, every round. Both are
     * spinning before `go`, so the two sets overlap. */
    for (int round = 0; round < 2000; round++) {
        sc_flag g = {0};
        atomic_int ready = 0;
        atomic_bool go = false;
        racer a = {&g, &ready, &go, false}, b = {&g, &ready, &go, false};
        sc_thread ta, tb;
        assert(sc_thread_start(&ta, race, &a, 0).ok);
        assert(sc_thread_start(&tb, race, &b, 0).ok);
        while (atomic_load(&ready) < 2) {
        }
        atomic_store(&go, true);
        assert(sc_thread_join(&ta).ok && sc_thread_join(&tb).ok);
        assert(a.won != b.won);
        assert(sc_flag_is_set(&g));
    }
}

/* Reports the thread's stack size, then touches every page of 3 MiB of it,
 * top down: more than the 512 KiB macOS gives a thread by default, so an
 * ignored size faults on the guard page there. */
static void deep(void *arg) {
    size_t *size = arg;
#ifdef __APPLE__
    *size = pthread_get_stacksize_np(pthread_self());
#else
    pthread_attr_t attr;
    assert(pthread_getattr_np(pthread_self(), &attr) == 0);
    assert(pthread_attr_getstacksize(&attr, size) == 0);
    pthread_attr_destroy(&attr);
#endif
    volatile uint8_t big[3u << 20];
    for (size_t i = sizeof big; i >= 1024; i -= 1024)
        big[i - 1] = (uint8_t)i;
    assert(big[sizeof big - 1] == (uint8_t)sizeof big);
}

static void noop(void *arg) {
    (void)arg;
}

static void test_thread_start_join(void) {
    size_t got = 0;
    sc_thread t;
    assert(sc_thread_start(&t, deep, &got, 4u << 20).ok);
    assert(t.running);
    assert(sc_thread_join(&t).ok);
    assert(got >= 4u << 20 && got < 5u << 20 && !t.running); /* not the 8 MiB default */
    sc_result_size again = sc_thread_join(&t);
    assert(!again.ok && again.err == EINVAL && again.value == 0);

    /* A tiny or zero request still starts: it rounds up to the minimum. */
    assert(sc_thread_start(&t, noop, NULL, 1).ok && sc_thread_join(&t).ok);
    assert(sc_thread_start(&t, noop, NULL, 0).ok && sc_thread_join(&t).ok);

    /* A NULL function or a size that overflows the rounding is refused. */
    t.running = true;
    sc_result_size bad = sc_thread_start(&t, NULL, NULL, 0);
    assert(!bad.ok && bad.err == EINVAL && !t.running);
    bad = sc_thread_start(&t, noop, NULL, SIZE_MAX);
    assert(!bad.ok && bad.err == EINVAL && !t.running);
    assert(round_up(1, 16384) == 16384 && round_up(16384, 16384) == 16384);
    assert(round_up(16385, 4096) == 20480 && round_up(SIZE_MAX, 4096) == 0);
}

static void test_clock(void) {
    uint64_t a = sc_clock_ms(), ua = sc_clock_us();
    sc_sleep_ms(25);
    uint64_t b = sc_clock_ms(), ub = sc_clock_us();
    assert(b >= a + 25 && b - a < 5000);
    assert(ub >= ua + 25000u && ub - ua < 5000000u);
    /* One clock: the two readings agree to within a millisecond or two. */
    assert(ub / 1000u + 2u >= b && b + 2u >= ub / 1000u);
    sc_sleep_ms(0);
    assert(sc_clock_ms() >= b);
}

int main(void) {
    test_wake_coalesces();
    test_wake_new_fails();
    test_wake_wait_eintr();
    test_wake_across_threads();
    test_flag();
    test_thread_start_join();
    test_clock();
    test_chan_with_wake();
    puts("thread_test: ok");
    return 0;
}
