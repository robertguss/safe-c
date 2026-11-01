#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include "../adapters/mem.c"
#include "../adapters/thread.c"
#include "../adapters/db_control.c"
#include "../examples/explorer/proto.c"
#include "../examples/explorer/budget.c"
#include "../examples/explorer/conn.c"
#include "../examples/explorer/conn_worker.c"
#include "../examples/explorer/result.c"
#include "../examples/explorer/wire.c"
#include "../examples/explorer/link.c"
#include "../examples/explorer/links.c"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

static sc_flag release_close[5];
static bool without_bye;

void worker_main(void *arg) {
    worker_ctx *ctx = arg;
    conn_worker machine;
    conn_actions actions;
    wire_side side;
    assert(ctx->conn < 5);
    assert(conn_worker_init(&machine, ctx->book, ctx->conn, ctx->max_grants));
    assert(conn_actions_init(&actions, ctx->book, ctx->max_actions));
    assert(wire_side_init(&side, ctx->handles, 64));
    bool ended = false;
    uint64_t deadline = sc_clock_ms() + 10000;
    while (!ended) {
        assert(sc_clock_ms() < deadline);
        size_t arena = 0;
        sc_chan_status status = wire_chan_recv(&side, ctx->requests, &arena);
        if (status == SC_CHAN_EMPTY) {
            assert(sc_wake_wait(ctx->wake, 1).ok);
            assert(sc_wake_drain(ctx->wake).ok);
            continue;
        }
        assert(status == SC_CHAN_OK);
        proto_header header;
        proto_reader reader;
        assert(proto_read(sc_mem_arena(ctx->pool, arena), &header, &reader));
        if (header.kind == PROTO_CLOSE) {
            while (!sc_flag_is_set(&release_close[ctx->conn])) {
                assert(sc_clock_ms() < deadline);
                sc_sleep_ms(1);
            }
            if (without_bye && ctx->conn == 2) {
                break;
            }
        }
        conn_worker_event event = {W_EV_MSG, arena, header, 0, 0};
        conn_actions_clear(&actions);
        assert(conn_worker_step(&machine, event, &actions));
        for (size_t i = 0; i < actions.count; i++) {
            conn_action action = conn_action_at(&actions, i);
            if (action.kind == ACT_SEND) {
                proto_writer writer;
                proto_header reply = {action.msg, action.conn, action.req, action.arg, action.aux, 0};
                assert(sc_mem_reset(ctx->pool, action.arena));
                assert(proto_begin(&writer, sc_mem_arena(ctx->pool, action.arena), reply));
                assert(proto_end(&writer));
                assert(wire_mbox_send(&side, ctx->replies, action.arena));
            } else if (action.kind == ACT_EXIT) {
                ended = true;
            } else {
                assert(action.kind == ACT_CONNECT || action.kind == ACT_FINISH);
            }
        }
    }
    (void)sc_flag_set(&ctx->done);
    ctx->ring(ctx->ring_arg);
}

static void no_ring(void *arg) {
    (void)arg;
}

static void sleep_wait(void *arg, uint32_t ms) {
    (void)arg;
    sc_sleep_ms(ms);
}

static void wait_returned(conn_link *link) {
    uint64_t deadline = sc_clock_ms() + 3000;
    while (!sc_flag_is_set(&link->wctx.done)) {
        assert(sc_clock_ms() < deadline);
        sc_sleep_ms(1);
    }
    (void)link_poll(link);
}

static void disconnect_then_reconnect(bool reclaim) {
    without_bye = reclaim;
    for (size_t i = 0; i < 5; i++) {
        sc_flag_clear(&release_close[i]);
    }
    link_pool pool;
    link_set set;
    conn_limits limits = {2, 4, 1000};
    sc_str empty = {0};
    assert(link_pool_init(&pool, budget_default_split()));
    links_init(&set, &pool, limits, no_ring, NULL, NULL, empty);
    assert(links_open(&set));
    conn_link *first = links_cur(&set);
    link_handoff handoff;
    bool handed = false;
    assert(links_reconnect(&set, &handoff, &handed) && handed);
    conn_link *second = links_cur(&set);
    assert(links_reconnect(&set, &handoff, &handed) && handed);
    assert(pool.book.retiring == 2 && !links_can_retire(&set));
    conn_link *current = links_cur(&set);
    result kept = {0};
    kept.rows = 123;
    size_t refused_free = pool.book.free;
    assert(!link_retire(current, &kept));
    assert(current->refused && current->refusal == CONN_REFUSE_RETIRING);
    assert(current->ui.state == UI_CONN_CONNECTING && !current->retired);
    assert(pool.book.retiring == 2 && pool.book.free == refused_free && kept.rows == 123);

    (void)sc_flag_set(&release_close[1]);
    wait_returned(first);
    assert(!first->broken && first->ui.state == UI_CONN_CLOSED && pool.book.retiring == 1);
    links_drain(&set);
    assert(links_can_retire(&set));
    assert(links_disconnect(&set, &handoff) && set.offline);
    assert(pool.book.retiring == 2 && !links_can_retire(&set));
    current = links_cur(&set);
    size_t free = pool.book.free;
    uint32_t next = set.next_conn;
    handoff.req = UINT64_MAX;
    assert(!links_reconnect(&set, &handoff, &handed) && !handed);
    assert(handoff.req == UINT64_MAX);
    assert(set.offline && links_cur(&set) == current && set.next_conn == next);
    assert(current->ui.state == UI_CONN_NONE && !current->started && !current->retired);
    assert(pool.book.retiring == 2 && pool.book.free == free);

    (void)sc_flag_set(&release_close[2]);
    wait_returned(second);
    assert(second->broken == reclaim);
    assert(pool.book.retiring == (reclaim ? 2u : 1u));
    links_drain(&set);
    assert(pool.book.retiring == 1);
    assert(links_reconnect(&set, &handoff, &handed) && !handed && !set.offline);
    assert(links_cur(&set) == current && current->started && !current->broken);
    assert(current->ui.state == UI_CONN_CONNECTING && pool.book.retiring == 1);
    (void)sc_flag_set(&release_close[3]);
    (void)sc_flag_set(&release_close[4]);
    assert(links_close(&set, 3000, sleep_wait, NULL));
    assert(pool.book.retiring == 0 && link_pool_used(&pool) == 0);
    link_pool_free(&pool);
}

int main(void) {
    disconnect_then_reconnect(false);
    disconnect_then_reconnect(true);
    puts("explorer_links_test: ok (BYE and reclaim recovery)");
    return 0;
}
