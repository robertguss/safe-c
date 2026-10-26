/* Tests the explorer's message wires (examples/explorer/wire.c): one handle per
 * message through sc_chan and sc_mbox, and the handle table's renewal once
 * every slot has retired. Core code, built in; one thread plays both ends. */
#include "../examples/explorer/wire.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>

static alignas(8) uint8_t tx_mem[4096], rx_mem[4096], ring_mem[4096];
static sc_arena tx_a, rx_a, ring_a;

/* More round trips than one slot's SC_HANDLE_GEN_MAX reuses, through a
 * one-slot table on each side, so both tables renew. */
static void renews_past_generation_max(void) {
    wire_side tx, rx;
    sc_chan c;
    sc_mbox m;
    sc_arena_init(&ring_a, ring_mem, sizeof ring_mem);
    assert(sc_chan_new(&c, &ring_a, 4) && sc_mbox_new(&m, &ring_a, 4));
    for (int pass = 0; pass < 2; pass++) {
        sc_arena_init(&tx_a, tx_mem, sizeof tx_mem);
        sc_arena_init(&rx_a, rx_mem, sizeof rx_mem);
        assert(wire_side_init(&tx, &tx_a, 1) && wire_side_init(&rx, &rx_a, 1));
        unsigned tx_epoch = tx_a.epoch, rx_epoch = rx_a.epoch;
        for (size_t i = 0; i < 3u * SC_HANDLE_GEN_MAX; i++) {
            size_t v = SIZE_MAX;
            if (pass == 0) {
                assert(wire_chan_send(&tx, &c, i));
                assert(wire_chan_recv(&rx, &c, &v) == SC_CHAN_OK);
            } else {
                assert(wire_mbox_send(&tx, &m, i));
                assert(wire_mbox_recv(&rx, &m, &v) == SC_MBOX_OK);
            }
            assert(v == i);
        }
        /* Each side reset its table's arena at least twice. */
        assert(tx_a.epoch >= tx_epoch + 2 && rx_a.epoch >= rx_epoch + 2);
        size_t v = 7;
        assert(pass == 1 || wire_chan_recv(&rx, &c, &v) == SC_CHAN_EMPTY);
        assert(pass == 0 || wire_mbox_recv(&rx, &m, &v) == SC_MBOX_EMPTY);
        assert(v == 7);
    }
}

/* A receiver whose only slot is taken gets NOSLOT from the channel; the wire
 * renews its table and the message still arrives. */
static void noslot_renews(void) {
    wire_side tx, rx;
    sc_chan c;
    sc_arena_init(&ring_a, ring_mem, sizeof ring_mem);
    sc_arena_init(&tx_a, tx_mem, sizeof tx_mem);
    sc_arena_init(&rx_a, rx_mem, sizeof rx_mem);
    assert(sc_chan_new(&c, &ring_a, 4));
    assert(wire_side_init(&tx, &tx_a, 1) && wire_side_init(&rx, &rx_a, 1));
    sc_handle held;
    assert(sc_handle_alloc(&rx_a, rx.table, &held)); /* the only slot */
    assert(wire_chan_send(&tx, &c, 42));
    size_t v = 0;
    assert(wire_chan_recv(&rx, &c, &v) == SC_CHAN_OK && v == 42);
    assert(!sc_handle_get(&rx_a, rx.table, held).some); /* the renewal retired it */
}

/* A full queue refuses the send and the sender keeps nothing. */
static void full_queue_refuses(void) {
    wire_side tx, rx;
    sc_chan c;
    sc_arena_init(&ring_a, ring_mem, sizeof ring_mem);
    sc_arena_init(&tx_a, tx_mem, sizeof tx_mem);
    sc_arena_init(&rx_a, rx_mem, sizeof rx_mem);
    assert(sc_chan_new(&c, &ring_a, 2));
    assert(wire_side_init(&tx, &tx_a, 1) && wire_side_init(&rx, &rx_a, 1));
    assert(wire_chan_send(&tx, &c, 1) && wire_chan_send(&tx, &c, 2));
    assert(!wire_chan_send(&tx, &c, 3));
    size_t v = 0;
    assert(wire_chan_recv(&rx, &c, &v) == SC_CHAN_OK && v == 1);
    assert(wire_chan_send(&tx, &c, 3)); /* the refused send held no slot */
    sc_chan_close(&c);
    assert(wire_chan_recv(&rx, &c, &v) == SC_CHAN_OK && v == 2);
    assert(wire_chan_recv(&rx, &c, &v) == SC_CHAN_OK && v == 3);
    assert(wire_chan_recv(&rx, &c, &v) == SC_CHAN_CLOSED);
}

int main(void) {
    renews_past_generation_max();
    noslot_renews();
    full_queue_refuses();
    puts("explorer_wire_test: ok");
    return 0;
}
