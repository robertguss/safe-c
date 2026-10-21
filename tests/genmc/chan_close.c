/* sc_chan: the sender sends N messages, closes, then tries once more; the
 * receiver polls until it sees CLOSED or its R polls run out. The send after
 * close returns CLOSED and keeps its handle. Once recv says CLOSED, every
 * accepted message has arrived, in order. */
#include "chan.h"
#ifndef CAP
#define CAP 1
#endif
#ifndef N
#define N 2
#endif
#ifndef R
#define R 5
#endif

static size_t acc[N], got[R];
static int naccepted, ngot, closed_seen;

static void *sender(void *arg) {
    (void)arg;
    for (int i = 0; i <= N; i++) {
        if (i == N)
            sc_chan_close(&ch);
        sc_handle h = take(&snd);
        sc_chan_status st = sc_chan_send(&ch, &snd.arena, snd.table, h, (size_t)i);
        assert(i < N || st == SC_CHAN_CLOSED);
        assert(live(&snd, h) == (st != SC_CHAN_OK));
        if (st == SC_CHAN_OK)
            acc[naccepted++] = (size_t)i;
        else
            drop(&snd, h);
    }
    return NULL;
}

static void *receiver(void *arg) {
    (void)arg;
    for (int i = 0; i < R && !closed_seen; i++) {
        sc_handle h;
        size_t v;
        sc_chan_status st = sc_chan_recv(&ch, &rcv.arena, rcv.table, &h, &v);
        if (st == SC_CHAN_OK) {
            got[ngot++] = v;
            drop(&rcv, h);
        } else if (st == SC_CHAN_CLOSED) {
            closed_seen = 1;
        } else {
            assert(st == SC_CHAN_EMPTY);
        }
    }
    return NULL;
}

int main(void) {
    chan_run(CAP, sender, receiver);
    /* No lost message: CLOSED is only reported once everything accepted arrived. */
    if (closed_seen)
        assert(ngot == naccepted);
    for (int k = 0; k < ngot; k++)
        assert(got[k] == acc[k]);
    return 0;
}
