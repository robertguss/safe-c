#ifndef SAFEC_TESTS_GENMC_CHAN_H
#define SAFEC_TESTS_GENMC_CHAN_H

/* The channel harnesses' shared state: one sender side, one receiver side, and
 * the channel, carved from the receiver's arena. */
#include "common.h"
#include "stdlib/chan.c"

static sc_chan ch;
static side snd, rcv;

/* Makes a channel of cap messages, then runs sender and receiver to completion. */
static void chan_run(size_t cap, void *(*sender)(void *), void *(*receiver)(void *)) {
    side_init(&snd, 1);
    side_init(&rcv, 1);
    bool ok = sc_chan_new(&ch, &rcv.arena, cap);
    assert(ok);
    run_pair(sender, receiver);
}

#endif
