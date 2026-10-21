#ifndef SAFEC_TESTS_GENMC_COMMON_H
#define SAFEC_TESTS_GENMC_COMMON_H

/* Scaffolding for the GenMC harnesses (scripts/genmc.py). Each thread owns an
 * arena and a handle table (tests/side.h), as sc_chan and sc_mbox require. The
 * stdlib sources are pulled in whole, from the tree passed with -I, so GenMC
 * checks the shipped code as one translation unit. */
#include "../side.h"
#include <pthread.h>
#include "stdlib/arena.c"
#include "stdlib/handle.c"
#include "stdlib/result.c"

static void drop(side *s, sc_handle h) {
    bool ok = sc_handle_free(&s->arena, s->table, h);
    assert(ok);
}

/* Runs a and b on their own threads and waits for both. */
static void run_pair(void *(*a)(void *), void *(*b)(void *)) {
    pthread_t ta, tb;
    pthread_create(&ta, NULL, a, NULL);
    pthread_create(&tb, NULL, b, NULL);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
}

#endif
