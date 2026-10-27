/* Tests the explorer's connection and query machines (examples/explorer/conn.c)
 * against the plan's U6 scenarios. Most run both machines through the
 * simulator in explorer_conn_sim.h, which carries their messages and checks
 * arena ownership after every step; a few play the worker by hand to send
 * replies a correct worker never would. Core code, built in. */
#include "../examples/explorer/proto.c"
#include "../examples/explorer/budget.c"
#include "../examples/explorer/conn.c"
#include "explorer_conn_sim.h"
#include <stdio.h>

static sim S;

static const conn_limits lim = {2, 4, 1000};

/* Runs a query and starts its column set. */
static void start_query(sim *s) {
    assert(sim_ui(s, CONN_UI_EV_RUN));
    sim_settle(s);
    assert(s->wk.state == W_RUNNING);
    assert(sim_pq(s, W_EV_COLUMNS, 0, 0));
    sim_settle(s);
    assert(s->ui.q == Q_STREAMING);
}

/* `n` rows sealed into a chunk, delivered. */
static void chunk_of(sim *s, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) {
        assert(sim_row(s, 1000, 0) == ACT_STORE_ROW);
    }
    assert(sim_pq(s, W_EV_TICK, 0, CONN_FLUSH_MS));
    sim_settle(s);
}

/* A reply for request 7 arrives after the tab ran request 8: dropped, its
 * arena freed, and the result model untouched. The worker is played by hand. */
static void stale_reply_is_dropped(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    start_query(&S);
    uint64_t r7 = S.ui.req;
    chunk_of(&S, 3);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_ROWS);

    assert(sim_ui(&S, CONN_UI_EV_RUN));
    sim_settle(&S);
    uint64_t r8 = S.ui.req;
    assert(r8 == r7 + 1);
    (void)r8;
    /* A grant for request 8, now with the worker, comes back labelled 7. */
    size_t g = SIZE_MAX;
    for (size_t i = 0; i < S.pool.count; i++) {
        if (S.owner[i] == S_WORKER && i != S.ui.terminal && i != S.ui.wrk) {
            g = i;
        }
    }
    assert(g != SIZE_MAX);
    query_state q = S.ui.q;
    size_t held = S.ui.held_count, out = S.ui.outstanding;
    uint64_t rows = S.ui.rows;
    size_t free = S.pool.free;
    size_t before = S.ulog_n;
    assert(sim_inject(&S, g, PROTO_CHUNK, r7, 50, 0));
    assert(S.ulog_n == before + 1 && S.ulog[before].kind == ACT_DROP && S.ulog[before].arena == g);
    assert(S.ui.q == q && S.ui.held_count == held && S.ui.outstanding == out && S.ui.rows == rows);
    assert(S.pool.free == free + 1);
    /* So is a stale terminal reply, in an arena that is not the live terminal. */
    size_t g2 = SIZE_MAX;
    for (size_t i = 0; i < S.pool.count; i++) {
        if (S.owner[i] == S_WORKER && i != S.ui.terminal && i != S.ui.wrk) {
            g2 = i;
        }
    }
    assert(g2 != SIZE_MAX);
    before = S.ulog_n;
    assert(sim_inject(&S, g2, PROTO_DONE, r7, 0, 0));
    assert(S.ulog_n == before + 1 && S.ulog[before].kind == ACT_DROP && S.ulog[before].arena == g2);
    assert(S.ui.q == q && S.ui.rows == rows && !S.ui.terminal_kept && S.ui.terminal_out);
    /* A message in the live terminal arena under another id, or a RETURN in
     * the worker's reserved arena, breaks the protocol: refused, not freed. */
    size_t t = S.ui.terminal;
    before = S.ulog_n;
    assert(!sim_inject(&S, t, PROTO_CHUNK, r7, 1, 0));
    assert(S.ulog_n == before && conn_pool_owner(&S.pool, t) == POOL_UI);
    assert(!sim_inject(&S, S.ui.wrk, PROTO_RETURN, r8, 0, 0));
    assert(S.ulog_n == before && conn_pool_owner(&S.pool, S.ui.wrk) == POOL_UI);
}

/* A GRANT for request 7 that crosses its DONE comes back as a RETURN after
 * request 8 started: dropped, and request 8's credit is untouched. */
static void stale_return_is_dropped(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    assert(sim_ui(&S, CONN_UI_EV_RUN));
    sim_settle(&S);
    uint64_t r7 = S.ui.req;
    assert(sim_pq(&S, W_EV_COLUMNS, 0, 0));
    while (sim_to_ui(&S)) {
    } /* the UI grants again; that GRANT is still queued to the worker */
    assert(S.to_worker.tail - S.to_worker.head == 1);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    while (sim_to_ui(&S)) {
    }
    assert(S.ui.q == Q_ROWS);
    assert(sim_ui(&S, CONN_UI_EV_RUN));
    size_t out8 = S.ui.outstanding;
    assert(sim_to_worker(&S)); /* GRANT(7) reaches a ready worker */
    conn_action ret = S.wlog[S.wlog_n - 1];
    assert(ret.kind == ACT_SEND && ret.msg == PROTO_RETURN && ret.req == r7);
    size_t before = S.ulog_n;
    assert(sim_to_ui(&S));
    assert(S.ulog[before].kind == ACT_DROP && S.ulog[before].arena == ret.arena);
    assert(S.ui.outstanding == out8);
    sim_settle(&S);
    assert(sim_pq(&S, W_EV_COLUMNS, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_STREAMING);
    sim_close(&S);
}

/* CANCEL for a request that already finished on the worker changes nothing
 * there; its arena comes back under the dead id, and pool occupancy is as it
 * was. DONE crossing the cancel moves the UI to Rows, not Cancelled. */
static void cancel_after_finish_is_stale(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    start_query(&S);
    chunk_of(&S, 2);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0)); /* DONE is queued to the UI */
    assert(S.wk.state == W_READY);
    assert(sim_ui(&S, CONN_UI_EV_CANCEL));
    assert(S.ui.q == Q_CANCELLING);
    conn_action c = sim_last(S.ulog, S.ulog_n, ACT_SEND, PROTO_CANCEL);
    assert(c.arena == S.ui.ctl);
    while (sim_to_ui(&S)) {
    }
    assert(S.ui.q == Q_ROWS);
    size_t wlog = S.wlog_n;
    assert(sim_to_worker(&S)); /* the CANCEL */
    assert(S.wk.state == W_READY);
    assert(S.wlog_n == wlog + 1 && S.wlog[wlog].msg == PROTO_RETURN &&
           S.wlog[wlog].arena == S.ui.ctl);
    sim_settle(&S);
    assert(S.ui.ctl_home && S.ui.q == Q_ROWS);
    /* The reserved pair, the terminal and the two arenas of the set. */
    assert(S.ui.held_count == 2);
    assert(S.pool.free == S.pool.count - 2 - 1 - 2);
    sim_close(&S);
}

/* LOST from an idle worker travels in its reserved arena; the UI moves to
 * Disconnected and hands the arena back so BYE can follow. */
static void lost_while_idle(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    assert(sim_pq(&S, W_EV_LOST, 0, 0));
    conn_action l = sim_last(S.wlog, S.wlog_n, ACT_SEND, PROTO_LOST);
    assert(l.arena == S.ui.wrk);
    assert(S.wk.state == W_CLOSING);
    sim_settle(&S);
    assert(S.ui.state == UI_CONN_DISCONNECTED);
    assert(sim_ui(&S, CONN_UI_EV_RUN) && S.last_refusal == CONN_REFUSE_NOT_CONNECTED);
    sim_close(&S);
    assert(S.pool.free == S.pool.count);
}

/* With every pool arena held by the result, CANCEL and CLOSE still go out in
 * the UI's control arena. */
static void control_arena_when_pool_is_full(void) {
    conn_limits three = {2, 3, 100000};
    sim_init(&S, 6, three); /* 2 reserved, 1 terminal, and the set's quota of 3 */
    sim_connect(&S);
    start_query(&S);
    chunk_of(&S, 1);
    assert(sim_row(&S, 1000, 0) == ACT_STORE_ROW);
    assert(sim_pq(&S, W_EV_TICK, 0, CONN_FLUSH_MS));
    while (sim_to_ui(&S)) {
    } /* the CANCEL below is still queued to the worker */
    assert(S.pool.free == 0);
    /* The set is full and no arena is free for the spare credit, so the UI
     * cancels for quota by itself, in the control arena. */
    assert(S.ui.held_count == 3 && S.ui.outstanding == 0 && S.ui.cancel_sent);
    conn_action c = sim_last(S.ulog, S.ulog_n, ACT_SEND, PROTO_CANCEL);
    assert(c.arena == S.ui.ctl && c.aux == PROTO_CAP_QUOTA);
    assert(!S.ui.ctl_home);
    assert(sim_ui(&S, CONN_UI_EV_CLOSE));
    assert(S.ui.close_pending); /* waits for the control arena */
    sim_settle(&S);
    assert(sim_count(S.ulog, 0, S.ulog_n, ACT_SEND, PROTO_CLOSE) == 1);
    assert(sim_last(S.ulog, S.ulog_n, ACT_SEND, PROTO_CLOSE).arena == S.ui.ctl);
    assert(S.exited && S.ui.state == UI_CONN_CLOSED);
    assert(S.ui.q == Q_DISCONNECTED && S.ui.rows == 2);
}

/* COLUMNS, CHUNK, COLUMNS, CHUNK, DONE: only the second set is left, the first
 * set's arenas are back in the pool, and the earlier tag is counted. */
static void second_set_replaces_first(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    start_query(&S);
    chunk_of(&S, 5);
    size_t first0 = conn_ui_held(&S.ui, 0), first1 = conn_ui_held(&S.ui, 1);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_COLUMNS, 0, 0));
    size_t mark = S.ulog_n;
    sim_settle(&S);
    assert(S.ui.held_count == 1 && S.ui.rows == 0);
    assert(S.ui.outstanding == lim.window); /* the ledger is back to the RUN grant */
    assert(sim_count(S.ulog, mark, S.ulog_n, ACT_FREE, PROTO_NONE) == 2);
    bool freed0 = false, freed1 = false;
    for (size_t i = mark; i < S.ulog_n; i++) {
        freed0 = freed0 || (S.ulog[i].kind == ACT_FREE && S.ulog[i].arena == first0);
        freed1 = freed1 || (S.ulog[i].kind == ACT_FREE && S.ulog[i].arena == first1);
    }
    assert(freed0 && freed1 && first0 != first1);
    chunk_of(&S, 2);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_ROWS && S.ui.rows == 2 && S.ui.held_count == 2);
    assert(S.ui.earlier_tags == 1);
    assert(S.ui.outstanding == 0);
    /* Only the reserved pair, the terminal and the second set are held. */
    assert(S.pool.free == S.pool.count - 2 - 1 - 2);
    sim_close(&S);
}

/* A first set that fills its quota exactly does not cap the next statement:
 * the spare credit carries its COLUMNS. */
static void full_first_set_then_second(void) {
    sim_init(&S, 16, lim); /* quota 4 */
    sim_connect(&S);
    start_query(&S);
    chunk_of(&S, 1);
    chunk_of(&S, 1);
    chunk_of(&S, 1);
    assert(S.ui.held_count == 4);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_COLUMNS, 0, 0));
    assert(S.wk.state == W_RUNNING);
    sim_settle(&S);
    assert(S.ui.held_count == 1 && S.ui.q == Q_STREAMING);
    chunk_of(&S, 2);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_ROWS && S.ui.rows == 2 && S.ui.earlier_tags == 1);
    sim_close(&S);
}

/* A row too big for an empty chunk caps for memory and is discarded once. */
static void oversized_row_discarded_once(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    start_query(&S);
    size_t mark = S.wlog_n;
    assert(sim_row(&S, 0, 0) == ACT_DISCARD); /* nothing fits */
    assert(sim_count(S.wlog, mark, S.wlog_n, ACT_DISCARD, PROTO_NONE) == 1);
    assert(sim_count(S.wlog, mark, S.wlog_n, ACT_CANCEL, PROTO_NONE) == 1);
    assert(sim_pq(&S, W_EV_ERROR, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_CAPPED && S.ui.cap == PROTO_CAP_MEMORY && S.ui.rows == 0);
    sim_close(&S);
}

/* A refused run leaves the shown result alone: with the pool short even after
 * freeing it, nothing is freed and the result still shows. */
static void refused_run_keeps_result(void) {
    conn_limits one = {1, 1, 100};
    sim_init(&S, 5, one); /* the reserved pair, a terminal, one grant and one more */
    sim_connect(&S);
    assert(sim_ui(&S, CONN_UI_EV_RUN));
    sim_settle(&S);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_COMMAND_TAG && S.ui.terminal_kept && S.pool.free == 2);
    /* Another connection's result takes a free arena: one free and the
     * terminal are two, short of a RUN, COLUMNS and a chunk. */
    size_t other = pool_take(&S.pool, 2, BUDGET_QUERY, BUDGET_USE_GRANT, 0, 0);
    assert(other != SIZE_MAX && S.pool.free == 1);
    S.owner[other] = S_UI;
    size_t mark = S.ulog_n;
    assert(sim_ui(&S, CONN_UI_EV_RUN));
    assert(S.last_refusal == CONN_REFUSE_POOL);
    assert(S.ulog_n == mark + 1); /* the refusal only: nothing freed or sent */
    assert(S.ui.q == Q_COMMAND_TAG && S.ui.terminal_kept);
}

/* A CHUNK under quota with no free pool arena makes the UI send CANCEL for
 * memory, and the request ends CAPPED (memory), not stuck in Streaming. */
static void dry_pool_caps_for_memory(void) {
    conn_limits big = {1, 20, 100000};
    sim_init(&S, 5, big); /* 2 reserved, 1 terminal, 2 chunks */
    sim_connect(&S);
    start_query(&S); /* COLUMNS used the one grant; a new one came back */
    assert(S.pool.free == 0);
    assert(sim_row(&S, 1000, 0) == ACT_STORE_ROW);
    assert(sim_pq(&S, W_EV_TICK, 0, CONN_FLUSH_MS));
    sim_settle(&S);
    conn_action c = sim_last(S.ulog, S.ulog_n, ACT_SEND, PROTO_CANCEL);
    assert(c.arena == S.ui.ctl && c.aux == PROTO_CAP_MEMORY);
    assert(S.wk.state == W_CANCELLING);
    assert(sim_row(&S, 1000, 0) == ACT_DISCARD);
    assert(sim_pq(&S, W_EV_ERROR, 0, 0)); /* 57014 */
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_CAPPED && S.ui.cap == PROTO_CAP_MEMORY && S.ui.rows == 1);
    sim_close(&S);
}

/* A user cancel that takes effect ends Cancelled with the rows kept. */
static void cancel_keeps_rows(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    start_query(&S);
    chunk_of(&S, 4);
    assert(sim_row(&S, 1000, 0) == ACT_STORE_ROW); /* in the open chunk */
    assert(sim_ui(&S, CONN_UI_EV_CANCEL));
    sim_settle(&S);
    assert(S.wk.state == W_CANCELLING);
    assert(sim_last(S.wlog, S.wlog_n, ACT_CANCEL, PROTO_NONE).kind == ACT_CANCEL);
    assert(sim_row(&S, 1000, 0) == ACT_DISCARD);
    assert(sim_pq(&S, W_EV_ERROR, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_CANCELLED && S.ui.rows == 5);
    sim_close(&S);
}

/* ERROR after two CHUNKs moves to ErrorAfterRows and keeps the rows. An error
 * with no rows is plain Error. */
static void error_after_rows(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    start_query(&S);
    chunk_of(&S, 3);
    chunk_of(&S, 4);
    assert(sim_row(&S, 1000, 0) == ACT_STORE_ROW);
    assert(sim_pq(&S, W_EV_ERROR, 0, 0));
    assert(S.wk.state == W_DRAINING);
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_ERROR_AFTER_ROWS && S.ui.rows == 8);
    conn_action e = sim_last(S.wlog, S.wlog_n, ACT_SEND, PROTO_ERROR);
    assert(e.arg == 8 && e.arena == S.ui.terminal);

    assert(sim_ui(&S, CONN_UI_EV_RUN));
    sim_settle(&S);
    assert(sim_pq(&S, W_EV_ERROR, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_ERROR && S.ui.rows == 0);
    /* Columns but no rows, then an error: the plain error block (plan U8). */
    start_query(&S);
    assert(sim_pq(&S, W_EV_ERROR, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_ERROR && S.ui.rows == 0);
    sim_close(&S);
}

/* The ledger never grants past quota: once the result holds its quota, a
 * CHUNK brings no GRANT, and the worker caps when it runs out of arenas with
 * rows left. */
static void quota_stops_grants(void) {
    sim_init(&S, 16, lim); /* window 2, quota 4 */
    sim_connect(&S);
    start_query(&S);
    chunk_of(&S, 1);
    chunk_of(&S, 1);
    size_t grants = sim_count(S.ulog, 0, S.ulog_n, ACT_SEND, PROTO_GRANT);
    chunk_of(&S, 1); /* the 4th arena the set holds */
    assert(S.ui.held_count == 4);
    assert(sim_count(S.ulog, 0, S.ulog_n, ACT_SEND, PROTO_GRANT) == grants);
    assert(S.ui.held_count + S.ui.outstanding == lim.quota + 1);
    assert(S.wk.grant_count == 1); /* the spare, never used for rows */
    assert(sim_row(&S, 1000, 0) == ACT_DISCARD);
    assert(S.wk.state == W_CANCELLING && S.wk.cap == PROTO_CAP_QUOTA);
    assert(sim_pq(&S, W_EV_ERROR, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_CAPPED && S.ui.cap == PROTO_CAP_QUOTA && S.ui.rows == 3);
    sim_close(&S);
}

/* The row limit caps the set: the row past it is dropped and the query is
 * cancelled. A set of exactly the limit is not capped. */
static void row_limit_caps(void) {
    conn_limits small = {2, 8, 3};
    sim_init(&S, 16, small);
    sim_connect(&S);
    start_query(&S);
    for (int i = 0; i < 3; i++) {
        assert(sim_row(&S, 1000, 0) == ACT_STORE_ROW);
    }
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_ROWS && S.ui.rows == 3);

    start_query(&S);
    for (int i = 0; i < 3; i++) {
        assert(sim_row(&S, 1000, 0) == ACT_STORE_ROW);
    }
    assert(sim_row(&S, 1000, 0) == ACT_DISCARD);
    assert(S.wk.cap == PROTO_CAP_LIMIT);
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_CAPPED && S.ui.cap == PROTO_CAP_LIMIT && S.ui.rows == 3);
    sim_close(&S);
}

/* A RUN takes its terminal arena first. With too few free arenas the run is
 * refused with "pool exhausted" and nothing is sent. */
static void run_needs_terminal_arena(void) {
    sim_init(&S, 3, lim); /* the reserved pair and one more */
    sim_connect(&S);
    size_t sends = sim_count(S.ulog, 0, S.ulog_n, ACT_SEND, PROTO_NONE);
    assert(sim_ui(&S, CONN_UI_EV_RUN));
    assert(S.refusals == 1 && S.last_refusal == CONN_REFUSE_POOL);
    assert(sim_count(S.ulog, 0, S.ulog_n, ACT_SEND, PROTO_NONE) == sends);
    assert(S.ui.q == Q_NONE);
    sim_close(&S);

    sim_init(&S, 16, lim);
    sim_connect(&S);
    assert(sim_ui(&S, CONN_UI_EV_RUN));
    conn_action r = sim_last(S.ulog, S.ulog_n, ACT_SEND, PROTO_RUN);
    assert(r.arena == S.ui.terminal && r.arg == lim.row_limit && r.aux == lim.quota);
    assert(sim_count(S.ulog, 0, S.ulog_n, ACT_SEND, PROTO_GRANT) == lim.window);
    assert(sim_ui(&S, CONN_UI_EV_RUN) && S.last_refusal == CONN_REFUSE_BUSY);
    sim_settle(&S);
    sim_close(&S);
}

/* LOST in every worker state moves it to closing; the UI moves to
 * Disconnected with the rows kept, and every arena still comes home. */
static void lost_in_every_state(void) {
    for (int st = 0; st < 5; st++) {
        sim_init(&S, 16, lim);
        assert(sim_ui(&S, CONN_UI_EV_OPEN));
        sim_settle(&S);
        uint64_t rows = 0;
        if (st > 0) {
            assert(sim_pq(&S, W_EV_CONNECTED, 0, 0));
            sim_settle(&S);
        }
        if (st > 1) {
            start_query(&S);
            chunk_of(&S, 2);
            assert(sim_row(&S, 1000, 0) == ACT_STORE_ROW);
            rows = 3;
        }
        if (st == 3) {
            assert(sim_ui(&S, CONN_UI_EV_CANCEL));
            sim_settle(&S);
            assert(S.wk.state == W_CANCELLING);
        }
        if (st == 4) {
            assert(sim_pq(&S, W_EV_ERROR, 0, 0));
            assert(S.wk.state == W_DRAINING);
        }
        assert(sim_pq(&S, W_EV_LOST, 0, 0));
        assert(S.wk.state == W_CLOSING);
        sim_settle(&S);
        assert(S.ui.state == UI_CONN_DISCONNECTED);
        assert(st < 2 || (S.ui.q == Q_DISCONNECTED && S.ui.rows == rows));
        assert(S.ui.outstanding == 0 && !S.ui.terminal_out);
        sim_close(&S);
    }
}

/* Statements with no columns end as a command tag. */
static void command_tag(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    assert(sim_ui(&S, CONN_UI_EV_RUN));
    sim_settle(&S);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_COMMAND_TAG && S.ui.earlier_tags == 1 && S.ui.held_count == 0);
    assert(S.pool.free == S.pool.count - 3);
    sim_close(&S);
}

/* A run while connecting is queued and goes out once connected; a failed
 * connect reports auth and network apart. */
static void queued_run_and_connect_failures(void) {
    sim_init(&S, 16, lim);
    assert(sim_ui(&S, CONN_UI_EV_OPEN));
    assert(sim_ui(&S, CONN_UI_EV_RUN));
    assert(S.ui.q == Q_QUEUED);
    sim_settle(&S);
    assert(sim_pq(&S, W_EV_CONNECTED, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_RUNNING && S.wk.state == W_RUNNING);
    sim_close(&S);

    for (uint64_t why = 0; why < PROTO_FAIL_COUNT; why++) {
        sim_init(&S, 16, lim);
        assert(sim_ui(&S, CONN_UI_EV_OPEN));
        sim_settle(&S);
        assert(sim_pq(&S, W_EV_CONN_FAILED, why, 0));
        assert(sim_last(S.wlog, S.wlog_n, ACT_SEND, PROTO_CONN_FAILED).arena == S.ui.wrk);
        sim_settle(&S);
        assert(S.ui.state == (why == PROTO_FAIL_AUTH ? UI_CONN_AUTH_FAILED : UI_CONN_UNREACHABLE));
        sim_close(&S);
        assert(S.pool.free == S.pool.count);
    }
}

/* Closing while streaming sends the open chunk like LOST does, returns every
 * arena and ends with BYE. */
static void close_while_streaming(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    start_query(&S);
    chunk_of(&S, 2);
    assert(sim_row(&S, 1000, 0) == ACT_STORE_ROW);
    sim_close(&S);
    assert(sim_last(S.wlog, S.wlog_n, ACT_FINISH, PROTO_NONE).kind == ACT_FINISH);
    assert(sim_last(S.wlog, S.wlog_n, ACT_SEND, PROTO_BYE).arena == S.ui.wrk);
    assert(S.ui.rows == 3);
}

/* The first chunk is sealed at 100 rows, later ones only when full or old. */
static void first_chunk_flushes_early(void) {
    conn_limits roomy = {2, 16, 100000};
    sim_init(&S, 16, roomy);
    sim_connect(&S);
    start_query(&S);
    for (unsigned i = 0; i < CONN_FIRST_CHUNK_ROWS; i++) {
        assert(sim_row(&S, 1000, 10) == ACT_STORE_ROW);
    }
    assert(sim_count(S.wlog, 0, S.wlog_n, ACT_SEND, PROTO_CHUNK) == 1);
    for (unsigned i = 0; i < CONN_FIRST_CHUNK_ROWS; i++) {
        assert(sim_row(&S, 1000, 20) == ACT_STORE_ROW);
    }
    assert(sim_count(S.wlog, 0, S.wlog_n, ACT_SEND, PROTO_CHUNK) == 1);
    assert(sim_pq(&S, W_EV_TICK, 0, 20 + CONN_FLUSH_MS - 1));
    assert(sim_count(S.wlog, 0, S.wlog_n, ACT_SEND, PROTO_CHUNK) == 1);
    assert(sim_pq(&S, W_EV_TICK, 0, 20 + CONN_FLUSH_MS));
    assert(sim_count(S.wlog, 0, S.wlog_n, ACT_SEND, PROTO_CHUNK) == 2);
    /* A row that does not fit seals the chunk and goes into the next. */
    sim_settle(&S);
    for (unsigned i = 0; i < 5; i++) {
        assert(sim_row(&S, 3, 30) == ACT_STORE_ROW);
    }
    assert(sim_count(S.wlog, 0, S.wlog_n, ACT_SEND, PROTO_CHUNK) == 3);
    sim_close(&S);
}

/* With no grant in hand and quota left, the worker waits for credit rather
 * than capping. */
static void worker_waits_for_credit(void) {
    conn_limits one = {1, 8, 100000};
    sim_init(&S, 16, one);
    sim_connect(&S);
    assert(sim_ui(&S, CONN_UI_EV_RUN));
    sim_settle(&S);
    assert(sim_pq(&S, W_EV_COLUMNS, 0, 0)); /* uses the only grant */
    assert(sim_row(&S, 1000, 0) == ACT_WAIT);
    sim_settle(&S); /* COLUMNS lands, the UI grants one more */
    assert(sim_row(&S, 1000, 0) == ACT_STORE_ROW);
    sim_close(&S);
}

/* Tabs (U12): a finished result passes to its tab with KEEP, and the next
 * run frees none of it; conn_pool_free_kept returns each kept arena, and
 * refuses one the machine still uses or that is not kept. */
static void keep_then_free(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    start_query(&S);
    chunk_of(&S, 3);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_ROWS && S.ui.held_count == 2 && S.ui.terminal_kept);
    size_t kept[3] = {conn_ui_held(&S.ui, 0), conn_ui_held(&S.ui, 1), S.ui.terminal};
    size_t free = S.pool.free;
    assert(sim_ui(&S, CONN_UI_EV_KEEP));
    assert(S.ui.held_count == 0 && !S.ui.terminal_kept && S.pool.free == free);
    assert(S.ui.q == Q_ROWS && S.ui.rows == 3); /* still describes the kept result */
    size_t before = S.ulog_n;
    start_query(&S); /* frees nothing it no longer holds */
    assert(sim_count(S.ulog, before, S.ulog_n, ACT_FREE, PROTO_NONE) == 0);
    chunk_of(&S, 2);
    /* Arenas the machine uses are refused, and nothing moves. */
    before = S.ulog_n;
    free = S.pool.free;
    assert(!conn_pool_free_kept(&S.pool, conn_ui_held(&S.ui, 1)));
    assert(!conn_pool_free_kept(&S.pool, S.ui.ctl));
    assert(!conn_pool_free_kept(&S.pool, S.ui.terminal));
    assert(S.ulog_n == before && S.pool.free == free);
    for (size_t i = 0; i < 3; i++) {
        assert(conn_pool_owner(&S.pool, kept[i]) == POOL_KEPT);
        assert(conn_pool_free_kept(&S.pool, kept[i]));
        S.owner[kept[i]] = S_FREE;
        sim_check(&S);
        assert(conn_pool_owner(&S.pool, kept[i]) == POOL_FREE);
    }
    assert(!conn_pool_free_kept(&S.pool, kept[0]));
    assert(!conn_pool_free_kept(&S.pool, 99));      /* past the pool */
    /* KEEP needs a finished query. */
    assert(!sim_ui(&S, CONN_UI_EV_KEEP) && S.ui.held_count == 2);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    /* After the connection closes, a kept result is still freed: a tab can
     * close after quit too. */
    assert(S.ui.q == Q_ROWS && S.ui.held_count == 2 && S.ui.terminal_kept);
    size_t late[3] = {conn_ui_held(&S.ui, 0), conn_ui_held(&S.ui, 1), S.ui.terminal};
    assert(sim_ui(&S, CONN_UI_EV_KEEP));
    assert(sim_ui(&S, CONN_UI_EV_CLOSE));
    sim_settle(&S);
    assert(S.exited && S.ui.state == UI_CONN_CLOSED);
    assert(sim_ui(&S, CONN_UI_EV_ABANDON)); /* nothing current: a no-op */
    for (size_t i = 0; i < 3; i++) {
        assert(conn_pool_free_kept(&S.pool, late[i]));
        S.owner[late[i]] = S_FREE;
    }
    sim_check(&S);
    assert(S.pool.free == S.pool.count);
}

/* Closing a tab while it streams frees its arenas at once and cancels; a
 * chunk already on its way and the terminal reply are freed on arrival, and
 * the pool is whole again once the worker reports the end. */
static void abandon_while_streaming(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    start_query(&S);
    chunk_of(&S, 4);
    for (int i = 0; i < 2; i++) {
        assert(sim_row(&S, 1000, 0) == ACT_STORE_ROW);
    }
    assert(sim_pq(&S, W_EV_TICK, 0, CONN_FLUSH_MS)); /* a CHUNK on its way */
    assert(S.to_ui.tail - S.to_ui.head >= 1);
    size_t held = S.ui.held_count, before = S.ulog_n;
    assert(held == 2);
    assert(sim_ui(&S, CONN_UI_EV_ABANDON));
    assert(sim_count(S.ulog, before, S.ulog_n, ACT_FREE, PROTO_NONE) == held);
    assert(sim_last(S.ulog, S.ulog_n, ACT_SEND, PROTO_CANCEL).msg == PROTO_CANCEL);
    assert(S.ui.held_count == 0 && S.ui.rows == 0 && S.ui.q == Q_CANCELLING);
    sim_settle(&S);
    assert(sim_row(&S, 1000, 0) == ACT_DISCARD);
    assert(sim_pq(&S, W_EV_ERROR, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_CANCELLED && S.ui.held_count == 0 && !S.ui.terminal_kept);
    assert(S.pool.free == S.pool.count - 2); /* only the reserved arenas are out */
    /* A finished result is abandoned at once. */
    start_query(&S);
    chunk_of(&S, 1);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_ROWS);
    assert(sim_ui(&S, CONN_UI_EV_ABANDON));
    assert(S.pool.free == S.pool.count - 2 && S.ui.q == Q_ROWS);
    /* A queued run is dropped. */
    sim_close(&S);
    sim_init(&S, 16, lim);
    assert(sim_ui(&S, CONN_UI_EV_OPEN));
    sim_settle(&S);
    assert(sim_ui(&S, CONN_UI_EV_RUN) && S.ui.q == Q_QUEUED);
    assert(sim_ui(&S, CONN_UI_EV_ABANDON) && S.ui.q == Q_NONE);
    assert(sim_pq(&S, W_EV_CONNECTED, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_NONE && S.wk.state == W_READY);
    sim_close(&S);
}

/* A RUN's row limit comes from the event, or the connection's when 0. */
static void run_limit_per_run(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    assert(sim_ui_ev(&S, CONN_UI_EV_RUN, 0, UINT64_MAX));
    assert(sim_last(S.ulog, S.ulog_n, ACT_SEND, PROTO_RUN).arg == UINT64_MAX);
    sim_settle(&S);
    assert(S.wk.row_limit == UINT64_MAX);
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(sim_ui_ev(&S, CONN_UI_EV_RUN, 0, 0));
    assert(sim_last(S.ulog, S.ulog_n, ACT_SEND, PROTO_RUN).arg == lim.row_limit);
    sim_settle(&S);
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    sim_close(&S);
}

/* ---- Cancel, disconnect and reconnect (plan U13) ---- */

/* No reply to a cancel: after 5 s in Cancelling the UI offers Disconnect, and
 * at the deadline the run reads "cancel failed". Disconnect retires the link:
 * the run ends Disconnected with its rows kept, and the CANCELLED that comes
 * after is dropped as stale. BYE gives the retiring slot back. */
static void cancel_deadline_then_disconnect(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    start_query(&S);
    chunk_of(&S, 2);
    assert(sim_ui_at(&S, CONN_UI_EV_CANCEL, 1000));
    assert(S.ui.q == Q_CANCELLING && !S.ui.offer_disconnect && !S.ui.cancel_failed);
    assert(sim_to_worker(&S)); /* the CANCEL: the worker cancels, and hears nothing back */
    assert(S.wk.state == W_CANCELLING);
    assert(sim_ui_at(&S, CONN_UI_EV_TICK, 1000 + CONN_DISCONNECT_OFFER_MS - 1));
    assert(!S.ui.offer_disconnect);
    assert(sim_ui_at(&S, CONN_UI_EV_TICK, 1000 + CONN_DISCONNECT_OFFER_MS));
    assert(S.ui.offer_disconnect && !S.ui.cancel_failed && S.ui.q == Q_CANCELLING);
    assert(sim_ui_at(&S, CONN_UI_EV_TICK, 1000 + CONN_CANCEL_DEADLINE_MS - 1));
    assert(!S.ui.cancel_failed);
    assert(sim_ui_at(&S, CONN_UI_EV_TICK, 1000 + CONN_CANCEL_DEADLINE_MS));
    assert(S.ui.cancel_failed && S.ui.offer_disconnect && S.ui.q == Q_CANCELLING);

    size_t held[2] = {conn_ui_held(&S.ui, 0), conn_ui_held(&S.ui, 1)};
    uint64_t req = S.ui.req;
    assert(conn_pool_can_retire(&S.pool));
    assert(sim_ui(&S, CONN_UI_EV_RETIRE));
    assert(S.ui.q == Q_DISCONNECTED && S.ui.rows == 2 && S.ui.state == UI_CONN_CLOSING);
    assert(S.ui.retiring && S.pool.retiring == 1);
    for (size_t i = 0; i < 2; i++) { /* the rows are the tab's now */
        assert(conn_pool_owner(&S.pool, held[i]) == POOL_KEPT);
        assert(budget_class_of(&S.budget, held[i]) != BUDGET_FROM_NONE);
    }
    /* What the link still has out is the retiring owner's, not the query's. */
    assert(budget_held(&S.budget, BUDGET_QUERY) == 2);
    assert(budget_held(&S.budget, BUDGET_RETIRING) > 0);
    /* The cancel lands late: the rows drain and CANCELLED arrives, dropped. */
    assert(sim_row(&S, 1000, 0) == ACT_DISCARD);
    assert(sim_pq(&S, W_EV_ERROR, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    conn_action c = sim_last(S.wlog, S.wlog_n, ACT_SEND, PROTO_CANCELLED);
    assert(c.req == req && c.arena == S.ui.terminal);
    size_t mark = S.ulog_n;
    sim_settle(&S);
    bool dropped = false;
    for (size_t i = mark; i < S.ulog_n; i++) {
        dropped = dropped || (S.ulog[i].kind == ACT_DROP && S.ulog[i].arena == c.arena);
    }
    assert(dropped && S.ui.q == Q_DISCONNECTED && !S.ui.terminal_kept);
    /* CLOSE went out with the retire; BYE frees the slot and every arena but
     * the tab's two. */
    assert(S.exited && S.ui.state == UI_CONN_CLOSED);
    assert(!S.ui.retiring && S.pool.retiring == 0);
    assert(budget_held(&S.budget, BUDGET_RETIRING) == 0);
    assert(S.pool.free == S.pool.count - 2);
}

/* The worker reports a cancel that failed: the run reads "cancel failed" and
 * Disconnect is offered at once. A later end still finishes it as Cancelled. */
static void worker_reports_failed_cancel(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    start_query(&S);
    chunk_of(&S, 1);
    assert(sim_ui_at(&S, CONN_UI_EV_CANCEL, 50));
    assert(sim_to_worker(&S));
    assert(sim_pq(&S, W_EV_CANCEL_FAILED, 0, 0));
    conn_action f = sim_last(S.wlog, S.wlog_n, ACT_SEND, PROTO_CANCEL_FAILED);
    assert(f.arena == S.ui.wrk && f.req == S.ui.req);
    sim_settle(&S);
    assert(S.ui.cancel_failed && S.ui.offer_disconnect && S.ui.q == Q_CANCELLING);
    assert(S.wk.have_reserved); /* the UI handed the arena back */
    assert(sim_pq(&S, W_EV_ERROR, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(S.ui.q == Q_CANCELLED && S.ui.rows == 1);
    sim_close(&S);
}

/* A worker that returns without BYE leaves its arenas charged until the UI
 * reclaims them: every arena of the connection comes back except a kept
 * result, which stays the tab's. */
static void reclaim_after_no_bye(void) {
    sim_init(&S, 16, lim);
    sim_connect(&S);
    start_query(&S);
    chunk_of(&S, 2);
    assert(sim_pq(&S, W_EV_TAG, 0, 0));
    assert(sim_pq(&S, W_EV_END, 0, 0));
    sim_settle(&S);
    assert(sim_ui(&S, CONN_UI_EV_KEEP)); /* a tab keeps COLUMNS, a chunk and the terminal */
    start_query(&S);
    chunk_of(&S, 1);
    assert(sim_row(&S, 1000, 0) == ACT_STORE_ROW); /* a chunk open with the worker */
    assert(S.pool.free < S.pool.count - 3);
    assert(sim_reclaim(&S));
    assert(S.ui.state == UI_CONN_CLOSED && S.ui.held_count == 0 && S.ui.outstanding == 0);
    assert(!S.ui.terminal_out && !S.ui.reserved);
    assert(S.pool.free == S.pool.count - 3);
    assert(budget_held(&S.budget, BUDGET_QUERY) == 3);
}

/* ---- Several links on one pool, played by hand ---- */

static alignas(8) uint8_t multi_mem[3][1u << 12], multi_act_mem[1u << 14], multi_budget_mem[1u << 12],
    multi_book_mem[1u << 12];

typedef struct {
    sc_arena book_a, act_a, budget_a, link_a[3];
    budget budget;
    conn_pool pool;
    conn_actions acts;
    conn_ui links[3];
} multi;

static multi M;

static void multi_init(void) {
    sc_arena_init(&M.book_a, multi_book_mem, sizeof multi_book_mem);
    sc_arena_init(&M.act_a, multi_act_mem, sizeof multi_act_mem);
    sc_arena_init(&M.budget_a, multi_budget_mem, sizeof multi_budget_mem);
    assert(budget_init(&M.budget, &M.budget_a, budget_default_split()));
    assert(conn_pool_init(&M.pool, &M.book_a, &M.budget));
    assert(conn_actions_init(&M.acts, &M.act_a, 64));
    const conn_limits app = {4, 128, 10000};
    for (size_t i = 0; i < 3; i++) {
        sc_arena_init(&M.link_a[i], multi_mem[i], sizeof multi_mem[i]);
        assert(conn_ui_init(&M.links[i], &M.link_a[i], (uint32_t)(i + 1u), BUDGET_QUERY, app));
    }
}

/* Steps link `i`, keeping the actions for the caller to read. */
static bool multi_step(size_t i, conn_ui_event_kind k, size_t arena, proto_kind msg, uint64_t req) {
    conn_actions_clear(&M.acts);
    conn_ui_event e = {k, arena, {msg, M.links[i].conn, req, 0, 0, 0}, 0};
    return conn_ui_step(&M.links[i], &M.pool, e, &M.acts);
}

static bool multi_refused(conn_refuse why) {
    for (size_t i = 0; i < M.acts.count; i++) {
        conn_action a = conn_action_at(&M.acts, i);
        if (a.kind == ACT_REFUSE && a.aux == (uint64_t)why) {
            return true;
        }
    }
    return false;
}

/* Two Reconnects against a host that never answers fill both retiring slots;
 * a third is refused, and neither the floor nor the new link's budget is
 * touched. When one retiring link says BYE, Reconnect works again. */
static void reconnects_fill_retiring_slots(void) {
    multi_init();
    size_t floor = budget_spare(&M.budget, BUDGET_FROM_FLOOR);
    size_t room = budget_room(&M.budget, BUDGET_QUERY);
    size_t meta = budget_room(&M.budget, BUDGET_META);
    /* Link 1 connects to the black hole; a run waits for it. */
    assert(multi_step(0, CONN_UI_EV_OPEN, 0, PROTO_NONE, 0));
    assert(multi_step(0, CONN_UI_EV_RUN, 0, PROTO_NONE, 0) && M.links[0].q == Q_QUEUED);
    /* Reconnect: link 1 retires (CLOSE in its control arena), link 2 opens. */
    assert(conn_pool_can_retire(&M.pool));
    assert(multi_step(0, CONN_UI_EV_RETIRE, 0, PROTO_NONE, 0));
    conn_action close1 = conn_action_at(&M.acts, M.acts.count - 1u);
    assert(close1.kind == ACT_SEND && close1.msg == PROTO_CLOSE && close1.arena == M.links[0].ctl);
    assert(M.links[0].q == Q_DISCONNECTED && M.links[0].state == UI_CONN_CLOSING);
    assert(multi_step(1, CONN_UI_EV_OPEN, 0, PROTO_NONE, 0));
    /* Reconnect again: link 2 retires, link 3 opens. */
    assert(conn_pool_can_retire(&M.pool));
    assert(multi_step(1, CONN_UI_EV_RETIRE, 0, PROTO_NONE, 0) && M.pool.retiring == 2);
    assert(multi_step(2, CONN_UI_EV_OPEN, 0, PROTO_NONE, 0));
    /* A third is refused: link 3 stays as it was and sends nothing. */
    assert(!conn_pool_can_retire(&M.pool));
    assert(multi_step(2, CONN_UI_EV_RETIRE, 0, PROTO_NONE, 0));
    assert(multi_refused(CONN_REFUSE_RETIRING) && M.acts.count == 1);
    assert(M.links[2].state == UI_CONN_CONNECTING && !M.links[2].retiring);
    /* The floor, the query link's room and the metadata link's are untouched. */
    assert(budget_spare(&M.budget, BUDGET_FROM_FLOOR) == floor);
    assert(budget_room(&M.budget, BUDGET_QUERY) == room);
    assert(budget_room(&M.budget, BUDGET_META) == meta);
    assert(budget_held(&M.budget, BUDGET_RETIRING) == 4); /* two control pairs */
    assert(budget_held(&M.budget, BUDGET_QUERY) == 2);    /* link 3's */
    assert(budget_spare(&M.budget, BUDGET_FROM_CONTROL) == 2); /* the metadata link's slot */
    /* Link 1's worker gives up its connect: the control arena comes back, then BYE. */
    assert(multi_step(0, CONN_UI_EV_MSG, M.links[0].ctl, PROTO_RETURN, 0));
    assert(multi_step(0, CONN_UI_EV_MSG, M.links[0].wrk, PROTO_BYE, 0));
    assert(M.links[0].state == UI_CONN_CLOSED && M.pool.retiring == 1);
    assert(budget_held(&M.budget, BUDGET_RETIRING) == 2);
    assert(conn_pool_can_retire(&M.pool));
    assert(multi_step(2, CONN_UI_EV_RETIRE, 0, PROTO_NONE, 0) && !multi_refused(CONN_REFUSE_RETIRING));
    assert(M.links[2].retiring && M.pool.retiring == 2);
}

int main(void) {
    cancel_deadline_then_disconnect();
    worker_reports_failed_cancel();
    reclaim_after_no_bye();
    reconnects_fill_retiring_slots();
    stale_reply_is_dropped();
    stale_return_is_dropped();
    full_first_set_then_second();
    oversized_row_discarded_once();
    refused_run_keeps_result();
    cancel_after_finish_is_stale();
    lost_while_idle();
    control_arena_when_pool_is_full();
    second_set_replaces_first();
    dry_pool_caps_for_memory();
    cancel_keeps_rows();
    error_after_rows();
    quota_stops_grants();
    row_limit_caps();
    run_needs_terminal_arena();
    lost_in_every_state();
    command_tag();
    queued_run_and_connect_failures();
    close_while_streaming();
    first_chunk_flushes_early();
    worker_waits_for_credit();
    keep_then_free();
    abandon_while_streaming();
    run_limit_per_run();
    puts("explorer_conn_test: ok");
    return 0;
}
