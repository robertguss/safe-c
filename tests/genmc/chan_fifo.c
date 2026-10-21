/* sc_chan: one sender sends N messages (a fresh handle each, no retry) while
 * one receiver polls R times. Every received message is one the sender had
 * accepted, in send order, each once. An accepted send leaves the handle stale
 * and a refused one leaves it live. After both threads join, the channel
 * drains everything accepted, then takes exactly CAP more and refuses one with
 * FULL. GenMC also fails on any race on the ring or the handle tables. */
#include "chan.h"
#ifndef CAP
#define CAP 1
#endif
#ifndef N
#define N 3
#endif
#ifndef R
#define R 3
#endif

static int accepted[N];
static size_t got[R + N];
static int ngot;

static sc_chan_status send_one(size_t value) {
    sc_handle h = take(&snd);
    sc_chan_status st = sc_chan_send(&ch, &snd.arena, snd.table, h, value);
    assert(live(&snd, h) == (st != SC_CHAN_OK));
    if (st != SC_CHAN_OK)
        drop(&snd, h);
    return st;
}

static sc_chan_status recv_one(size_t *value) {
    sc_handle h;
    sc_chan_status st = sc_chan_recv(&ch, &rcv.arena, rcv.table, &h, value);
    if (st == SC_CHAN_OK) {
        assert(live(&rcv, h));
        drop(&rcv, h);
    }
    return st;
}

static void *sender(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++) {
        sc_chan_status st = send_one((size_t)(100 + i));
        assert(st == SC_CHAN_OK || st == SC_CHAN_FULL);
        accepted[i] = st == SC_CHAN_OK;
    }
    return NULL;
}

static void *receiver(void *arg) {
    (void)arg;
    for (int i = 0; i < R; i++) {
        size_t v;
        sc_chan_status st = recv_one(&v);
        assert(st == SC_CHAN_OK || st == SC_CHAN_EMPTY);
        if (st == SC_CHAN_OK)
            got[ngot++] = v;
    }
    return NULL;
}

/* Received payloads are exactly the accepted ones, in order, each once. */
static void check_fifo(void) {
    int next = 0;
    for (int k = 0; k < ngot; k++) {
        while (next < N && !accepted[next])
            next++;
        assert(next < N && got[k] == (size_t)(100 + next));
        next++;
    }
    while (next < N)
        assert(!accepted[next++]);
}

int main(void) {
    chan_run(CAP, sender, receiver);
    size_t v;
    while (recv_one(&v) == SC_CHAN_OK)
        got[ngot++] = v;
    check_fifo();
    for (int k = 0; k <= CAP; k++)
        assert(send_one((size_t)(1000 + k)) == (k < CAP ? SC_CHAN_OK : SC_CHAN_FULL));
    for (int k = 0; k <= CAP; k++) {
        sc_chan_status st = recv_one(&v);
        assert(st == (k < CAP ? SC_CHAN_OK : SC_CHAN_EMPTY));
        assert(st != SC_CHAN_OK || v == (size_t)(1000 + k));
    }
    return 0;
}
