/* sc_mbox: S senders each send N messages (a fresh handle each, no retry)
 * while the receiver polls R times and, when CLOSE_AT >= 0, closes before poll
 * CLOSE_AT and stops at the first CLOSED. Each message arrives at most once and
 * only if accepted, one sender's messages arrive in its send order, and a
 * refused send (FULL or CLOSED) keeps its handle, FULL comes only when the
 * senders together can fill it, while an accepted one leaves it
 * stale. After the join every accepted message is delivered, CLOSED stays final,
 * and an open mailbox still takes exactly CAP more and refuses one with FULL.
 * GenMC also fails on any race on a slot's value or the handle tables. */
#include "common.h"
#include "stdlib/mailbox.c"
#ifndef CAP
#define CAP 1
#endif
#ifndef S
#define S 2
#endif
#ifndef N
#define N 2
#endif
#ifndef R
#define R 4
#endif
#ifndef CLOSE_AT
#define CLOSE_AT -1
#endif

static sc_mbox mb;
static side snd[S], rcv;
static int accepted[S][N];
static size_t got[R + S * N + 1];
static int ngot, closed_seen;

static sc_mbox_status send_one(side *s, size_t value) {
    sc_handle h = take(s);
    sc_mbox_status st = sc_mbox_send(&mb, &s->arena, s->table, h, value);
    assert(st == SC_MBOX_OK || st == SC_MBOX_FULL || st == SC_MBOX_CLOSED);
    assert(live(s, h) == (st != SC_MBOX_OK));
    if (st != SC_MBOX_OK)
        drop(s, h);
    return st;
}

static sc_mbox_status recv_one(size_t *value) {
    sc_handle h;
    sc_mbox_status st = sc_mbox_recv(&mb, &rcv.arena, rcv.table, &h, value);
    if (st == SC_MBOX_OK) {
        assert(live(&rcv, h));
        drop(&rcv, h);
    }
    return st;
}

static void *sender(void *arg) {
    int s = (int)(intptr_t)arg;
    for (int i = 0; i < N; i++) {
        sc_mbox_status st = send_one(&snd[s], (size_t)(s * 16 + i + 1));
        assert(S * N > CAP || st != SC_MBOX_FULL); /* FULL only when it can be full */
        accepted[s][i] = st == SC_MBOX_OK;
    }
    return NULL;
}

static void *receiver(void *arg) {
    (void)arg;
    for (int i = 0; i < R + (CLOSE_AT >= 0) && !closed_seen; i++) {
        if (i == CLOSE_AT)
            sc_mbox_close(&mb);
        size_t v;
        sc_mbox_status st = recv_one(&v);
        if (st == SC_MBOX_OK) {
            got[ngot++] = v;
        } else if (st == SC_MBOX_CLOSED) {
            assert(CLOSE_AT >= 0 && i >= CLOSE_AT);
            closed_seen = 1;
        } else {
            assert(st == SC_MBOX_EMPTY);
        }
    }
    return NULL;
}

/* All threads are joined, so every accepted send is published: drain it. */
static void drain(void) {
    for (;;) {
        size_t v;
        sc_mbox_status st = recv_one(&v);
        if (st != SC_MBOX_OK) {
            assert(st == (CLOSE_AT >= 0 ? SC_MBOX_CLOSED : SC_MBOX_EMPTY));
            return;
        }
        assert(!closed_seen); /* CLOSED was final: nothing arrives after it */
        got[ngot++] = v;
    }
}

/* Delivered only if accepted, at most once, in each sender's order, and nothing lost. */
static void check_delivery(void) {
    int last[S], seen[S][N] = {{0}}, total = 0;
    for (int s = 0; s < S; s++)
        last[s] = -1;
    for (int k = 0; k < ngot; k++) {
        int s = (int)((got[k] - 1) / 16), i = (int)((got[k] - 1) % 16);
        assert(s >= 0 && s < S && i < N);
        assert(accepted[s][i] && !seen[s][i]);
        seen[s][i] = 1;
        assert(i > last[s]);
        last[s] = i;
    }
    for (int s = 0; s < S; s++)
        for (int i = 0; i < N; i++)
            total += accepted[s][i];
    assert(ngot == total);
}

/* The empty, open mailbox takes CAP, refuses one more, and gives them back in order. */
static void refill(void) {
    for (int k = 0; k <= CAP; k++)
        assert(send_one(&snd[0], (size_t)(1000 + k)) == (k < CAP ? SC_MBOX_OK : SC_MBOX_FULL));
    for (int k = 0; k <= CAP; k++) {
        size_t v;
        sc_mbox_status st = recv_one(&v);
        assert(st == (k < CAP ? SC_MBOX_OK : SC_MBOX_EMPTY));
        assert(st != SC_MBOX_OK || v == (size_t)(1000 + k));
    }
}

int main(void) {
    for (int s = 0; s < S; s++)
        side_init(&snd[s], 1);
    side_init(&rcv, 1);
    bool ok = sc_mbox_new(&mb, &rcv.arena, CAP);
    assert(ok);
    pthread_t t[S], r;
    for (int s = 0; s < S; s++)
        pthread_create(&t[s], NULL, sender, (void *)(intptr_t)s);
    pthread_create(&r, NULL, receiver, NULL);
    for (int s = 0; s < S; s++)
        pthread_join(t[s], NULL);
    pthread_join(r, NULL);
    drain();
    check_delivery();
    if (CLOSE_AT < 0)
        refill();
    return 0;
}
