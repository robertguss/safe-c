#include "safec/arena.h"
#include "safec/chan.h"
#include "safec/handle.h"
#include <assert.h>
#include <stdalign.h>

#define CAP_MAX 4
#define SLOTS 4
#define KEPT 8

/* Reference model: a plain FIFO of payloads, a closed flag and the number of
 * receiver slots in use, compared with the channel after every operation. */
typedef struct {
    size_t q[CAP_MAX];
    size_t len;
    size_t cap;
    bool closed;
} model;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1)
        return 0;
    alignas(8) uint8_t sstore[128], rstore[64];
    sc_arena sa, ra;
    sc_arena_init(&sa, sstore, sizeof sstore);
    sc_arena_init(&ra, rstore, sizeof rstore);
    sc_handle_table st = sc_handle_table_new(&sa, SLOTS);
    sc_handle_table rt = sc_handle_table_new(&ra, SLOTS);
    assert(st.cap == SLOTS && rt.cap == SLOTS);

    model m = {{0}, 0, 0, false};
    size_t want = data[0] % (CAP_MAX + 1); /* 0 is refused */
    sc_chan c;
    bool made = sc_chan_new(&c, &sa, want);
    assert(made == (want >= 1));
    if (!made)
        return 0;
    m.cap = want;

    sc_handle sent[KEPT]; /* handles the sender once held, for use-after-send */
    size_t nsent = 0;
    sc_handle held[KEPT]; /* the sender's live, unsent handles */
    size_t nheld = 0;
    sc_handle recvd[KEPT]; /* the receiver's live handles */
    size_t nrecvd = 0;
    size_t next_value = 1;
    for (size_t p = 1; p + 1 < size; p += 2) {
        uint8_t op = data[p] % 6;
        uint8_t arg = data[p + 1];
        if (op == 0 && nheld < KEPT) { /* Alloc */
            sc_handle h;
            if (sc_handle_alloc(&sa, st, &h))
                held[nheld++] = h;
        } else if (op == 1) { /* Send: a held handle, a sent one, or neither */
            sc_handle h;
            bool fresh = nheld > 0 && arg % 3 != 0;
            if (fresh)
                h = held[arg % nheld];
            else if (nsent > 0)
                h = sent[arg % nsent];
            else
                h = (sc_handle){arg % SLOTS, arg, sa.epoch, 0};
            bool is_live = sc_handle_get(&sa, st, h).some;
            sc_chan_status got = sc_chan_send(&c, &sa, st, h, next_value);
            if (!is_live)
                assert(got == SC_CHAN_STALE);
            else if (m.closed)
                assert(got == SC_CHAN_CLOSED);
            else if (m.len == m.cap)
                assert(got == SC_CHAN_FULL);
            else
                assert(got == SC_CHAN_OK);
            if (got == SC_CHAN_OK) {
                m.q[m.len++] = next_value++;
                assert(!sc_handle_get(&sa, st, h).some); /* sender's copy is stale */
                if (nsent < KEPT)
                    sent[nsent++] = h;
                for (size_t i = 0; i < nheld; i++) {
                    if (held[i].index == h.index) {
                        held[i] = held[--nheld];
                        break;
                    }
                }
            } else if (is_live) {
                assert(sc_handle_get(&sa, st, h).some); /* refused: still the sender's */
            }
        } else if (op == 2) { /* Recv */
            sc_handle out = {91, 91, 91, 91};
            size_t v = 777;
            sc_chan_status got = sc_chan_recv(&c, &ra, rt, &out, &v);
            if (m.len == 0) {
                assert(got == (m.closed ? SC_CHAN_CLOSED : SC_CHAN_EMPTY));
            } else if (got == SC_CHAN_NOSLOT) {
                assert(nrecvd == SLOTS);
            } else {
                assert(got == SC_CHAN_OK && nrecvd < SLOTS);
            }
            if (got == SC_CHAN_OK) {
                assert(v == m.q[0]); /* FIFO */
                for (size_t i = 1; i < m.len; i++)
                    m.q[i - 1] = m.q[i];
                m.len--;
                assert(sc_handle_get(&ra, rt, out).some);
                if (nrecvd < KEPT)
                    recvd[nrecvd++] = out;
            } else {
                assert(out.index == 91 && v == 777);
            }
        } else if (op == 3 && nrecvd > 0) { /* Free (receiver) */
            size_t i = arg % nrecvd;
            assert(sc_handle_free(&ra, rt, recvd[i]));
            assert(!sc_handle_get(&ra, rt, recvd[i]).some);
            recvd[i] = recvd[--nrecvd];
        } else if (op == 4 && nheld > 0) { /* Free (sender, unsent) */
            size_t i = arg % nheld;
            assert(sc_handle_free(&sa, st, held[i]));
            held[i] = held[--nheld];
        } else if (op == 5) { /* Close */
            sc_chan_close(&c);
            m.closed = true;
        }
    }

    /* Drain: closed or not, queued messages arrive in order. */
    sc_chan_close(&c);
    m.closed = true;
    while (m.len > 0) {
        sc_handle out;
        size_t v;
        if (nrecvd == SLOTS) {
            assert(sc_handle_free(&ra, rt, recvd[--nrecvd]));
        }
        assert(sc_chan_recv(&c, &ra, rt, &out, &v) == SC_CHAN_OK);
        assert(v == m.q[0]);
        for (size_t i = 1; i < m.len; i++)
            m.q[i - 1] = m.q[i];
        m.len--;
        recvd[nrecvd++] = out;
    }
    sc_handle out;
    size_t v;
    assert(sc_chan_recv(&c, &ra, rt, &out, &v) == SC_CHAN_CLOSED);
    return 0;
}
