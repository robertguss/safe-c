/* Tests the explorer's pool budgets (examples/explorer/budget.c) against the
 * plan's U17 scenarios, with the KTD19 starting split: alone, and under the
 * connection machines through the simulator in explorer_conn_sim.h, which
 * checks after every step that each free arena is one a class can pay for.
 * Core code, built in. */
#include "../examples/explorer/proto.c"
#include "../examples/explorer/budget.c"
#include "../examples/explorer/conn.c"
#include "explorer_conn_sim.h"
#include <stdio.h>

static sim S;

static const conn_limits app = {4, 128, 10000}; /* main.c's query link */

/* Feeds rows of one chunk each until the worker stops storing them: it caps
 * (DISCARD) or waits with nothing left to grant. Credit is delivered as it
 * comes. The logs are cleared as it goes: a full result outgrows them. */
static act_kind stream(sim *s) {
    for (size_t n = 0; n < 100000; n++) {
        s->wlog_n = 0;
        s->ulog_n = 0;
        act_kind k = sim_row(s, 1, 0);
        if (k == ACT_WAIT) {
            sim_settle(s);
        } else if (k != ACT_STORE_ROW) {
            return k;
        }
    }
    assert(0 && "the stream never ended");
    return ACT_NONE;
}

/* A "Load all" run: COLUMNS, then rows until the worker caps, then the end. */
static void load_all(sim *s) {
    assert(sim_ui_ev(s, CONN_UI_EV_RUN, 0, UINT64_MAX));
    sim_settle(s);
    assert(sim_pq(s, W_EV_COLUMNS, 0, 0));
    sim_settle(s);
    assert(stream(s) == ACT_DISCARD);
    assert(sim_pq(s, W_EV_END, 0, 0));
    sim_settle(s);
}

/* The budget alone: where each use is paid from, and what take and put
 * refuse. */
static void picks_and_counts(void) {
    static alignas(8) uint8_t mem[4096];
    sc_arena a;
    sc_arena_init(&a, mem, sizeof mem);
    budget b;
    assert(budget_init(&b, &a, budget_default_split()));
    assert(b.count == 192);
    assert(budget_room(&b, BUDGET_QUERY) == 168 && budget_room(&b, BUDGET_META) == 16);
    assert(budget_limit(&b, BUDGET_QUERY) == 168 && budget_limit(&b, BUDGET_META) == 16);
    /* A run starts from the floor; grants come from growth. */
    assert(budget_pick(&b, BUDGET_QUERY, BUDGET_USE_RUN, 0, 0) == BUDGET_FROM_FLOOR);
    assert(budget_pick(&b, BUDGET_QUERY, BUDGET_USE_GRANT, 5, 3) == BUDGET_FROM_GROWTH);
    assert(budget_pick(&b, BUDGET_META, BUDGET_USE_RUN, 0, 0) == BUDGET_FROM_META);
    assert(budget_pick(&b, BUDGET_META, BUDGET_USE_CONTROL, 0, 0) == BUDGET_FROM_CONTROL);
    /* Results hold every growth arena. */
    for (size_t i = 0; i < 152; i++) {
        assert(budget_take(&b, i, BUDGET_QUERY, BUDGET_FROM_GROWTH));
    }
    assert(!budget_take(&b, 152, BUDGET_QUERY, BUDGET_FROM_GROWTH)); /* growth is full */
    assert(!budget_take(&b, 3, BUDGET_QUERY, BUDGET_FROM_FLOOR));    /* already charged */
    /* The floor pays for a set's COLUMNS and first chunk, one at a time. */
    assert(budget_pick(&b, BUDGET_QUERY, BUDGET_USE_GRANT, 0, 0) == BUDGET_FROM_FLOOR);
    assert(budget_pick(&b, BUDGET_QUERY, BUDGET_USE_GRANT, 1, 0) == BUDGET_FROM_FLOOR);
    assert(budget_pick(&b, BUDGET_QUERY, BUDGET_USE_GRANT, 1, 1) == BUDGET_FROM_NONE);
    assert(budget_pick(&b, BUDGET_QUERY, BUDGET_USE_GRANT, 2, 0) == BUDGET_FROM_NONE);
    assert(budget_pick(&b, BUDGET_QUERY, BUDGET_USE_RUN, 0, 0) == BUDGET_FROM_FLOOR);
    for (size_t i = 152; i < 168; i++) {
        assert(budget_take(&b, i, BUDGET_QUERY, BUDGET_FROM_FLOOR));
    }
    assert(budget_pick(&b, BUDGET_QUERY, BUDGET_USE_RUN, 0, 0) == BUDGET_FROM_NONE);
    assert(budget_room(&b, BUDGET_QUERY) == 0);
    /* The metadata link's 16 are untouched, and charge only it. */
    assert(budget_room(&b, BUDGET_META) == 16 && budget_held(&b, BUDGET_QUERY) == 168);
    for (size_t i = 168; i < 184; i++) {
        assert(budget_pick(&b, BUDGET_META, BUDGET_USE_GRANT, 0, 0) == BUDGET_FROM_META);
        assert(budget_take(&b, i, BUDGET_META, BUDGET_FROM_META));
    }
    assert(budget_pick(&b, BUDGET_META, BUDGET_USE_GRANT, 0, 0) == BUDGET_FROM_NONE);
    assert(budget_held(&b, BUDGET_META) == 16 && budget_held(&b, BUDGET_QUERY) == 168);
    /* An arena goes back to the class that paid for it. */
    assert(budget_class_of(&b, 160) == BUDGET_FROM_FLOOR);
    assert(budget_put(&b, 160) && budget_spare(&b, BUDGET_FROM_FLOOR) == 1);
    assert(budget_class_of(&b, 160) == BUDGET_FROM_NONE);
    assert(!budget_put(&b, 160)); /* not charged */
    assert(!budget_put(&b, 192)); /* past the pool */
    assert(budget_put(&b, 170) && budget_spare(&b, BUDGET_FROM_META) == 1);
    assert(budget_held(&b, BUDGET_META) == 15 && budget_held(&b, BUDGET_QUERY) == 167);
    assert(budget_spare(&b, BUDGET_FROM_GROWTH) == 0);
    puts("ok picks, takes and puts by class and owner");
}

/* One Load-all result reaches its 128-arena quota; a second, in another tab,
 * ends CAPPED (memory limit) at the 24 growth arenas left. Then, with every
 * growth arena held, SELECT 1 starts and completes from the floor, and a run
 * that needs a second chunk ends CAPPED (memory limit). */
static void two_load_alls_then_the_floor(void) {
    sim_init_split(&S, budget_default_split(), BUDGET_QUERY, app);
    sim_connect(&S);
    load_all(&S);
    assert(S.ui.q == Q_CAPPED && S.ui.cap == PROTO_CAP_QUOTA && S.ui.held_count == 128);
    assert(sim_ui(&S, CONN_UI_EV_KEEP)); /* the first tab keeps it */
    load_all(&S);
    assert(S.ui.q == Q_CAPPED && S.ui.cap == PROTO_CAP_MEMORY && S.ui.held_count == 24);
    assert(sim_ui(&S, CONN_UI_EV_KEEP)); /* the second tab keeps it */
    assert(budget_spare(&S.budget, BUDGET_FROM_GROWTH) == 0);
    assert(S.budget.used.floor == 2); /* the two terminal replies */

    /* SELECT 1: its RUN, COLUMNS and chunk from the floor, one grant at a time. */
    assert(sim_ui(&S, CONN_UI_EV_RUN));
    assert(S.ui.outstanding == 1 && S.budget.used.floor == 4);
    sim_settle(&S);
    assert(sim_pq(&S, W_EV_COLUMNS, 0, 0));
    sim_settle(&S);
    assert(S.ui.outstanding == 1 && S.budget.used.floor == 5);
    assert(sim_row(&S, 1000, 0) == ACT_STORE_ROW);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_ROWS && S.ui.rows == 1 && S.ui.held_count == 2);

    /* A second chunk has nothing to pay for it. */
    assert(sim_ui(&S, CONN_UI_EV_RUN));
    sim_settle(&S);
    assert(sim_pq(&S, W_EV_COLUMNS, 0, 0));
    sim_settle(&S);
    assert(stream(&S) == ACT_DISCARD);
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_CAPPED && S.ui.cap == PROTO_CAP_MEMORY && S.ui.held_count == 2);
    assert(S.ui.rows == 1);
    assert(sim_ui(&S, CONN_UI_EV_CLOSE));
    sim_settle(&S);
    assert(S.exited && S.ui.state == UI_CONN_CLOSED);
    puts("ok second Load all capped at 24; SELECT 1 from the floor; a second chunk caps");
}

/* The metadata link draws only on its own 16 arenas: its result caps for
 * memory there, and nothing is charged to the query link. */
static void metadata_has_its_own_budget(void) {
    sim_init_split(&S, budget_default_split(), BUDGET_META, app);
    sim_connect(&S);
    load_all(&S);
    assert(S.ui.q == Q_CAPPED && S.ui.cap == PROTO_CAP_MEMORY);
    assert(S.ui.held_count == 15); /* 16, less the terminal reply */
    assert(budget_held(&S.budget, BUDGET_QUERY) == 0);
    assert(S.budget.used.floor == 0 && S.budget.used.growth == 0);
    assert(budget_held(&S.budget, BUDGET_META) == 2 + 16);
    assert(budget_room(&S.budget, BUDGET_QUERY) == 168);
    sim_close(&S);
    puts("ok the metadata link's grants come from its own budget");
}

int main(void) {
    picks_and_counts();
    two_load_alls_then_the_floor();
    metadata_has_its_own_budget();
    puts("explorer_budget_test: ok");
    return 0;
}
