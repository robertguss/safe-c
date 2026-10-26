/* A simulator for the explorer's two connection machines
 * (examples/explorer/conn.c): it performs their actions the way main.c and
 * worker.c will, carrying messages through two FIFO queues in place of sc_chan
 * and sc_mbox, and tracks who holds each pool arena independently of the UI's
 * book. After every step it checks that the two agree, so a machine that sends
 * an arena it does not hold, frees one twice, or loses one fails at once.
 *
 * The test or fuzz harness includes conn.c (and proto.c) before this file. */
#ifndef EXPLORER_CONN_SIM_H
#define EXPLORER_CONN_SIM_H

#include <assert.h>
#include <stdalign.h>
#include <string.h>

#ifdef SIM_TRACE
#include <stdio.h>
#define SIM_T(...) fprintf(stderr, __VA_ARGS__)
#else
#define SIM_T(...) ((void)0)
#endif

#define SIM_MAX_ARENAS 64
#define SIM_QUEUE 256
#define SIM_LOG 512

/* Who holds an arena in the simulation. */
enum { S_FREE, S_UI, S_TO_WORKER, S_WORKER, S_TO_UI };

typedef struct {
    size_t arena;
    proto_header h;
} sim_msg;

typedef struct {
    sim_msg items[SIM_QUEUE];
    size_t head, tail;
} sim_queue;

typedef struct {
    alignas(8) uint8_t book_mem[1u << 14];
    alignas(8) uint8_t ui_mem[1u << 14];
    alignas(8) uint8_t wk_mem[1u << 14];
    alignas(8) uint8_t act_mem[1u << 16];
    sc_arena book_a, ui_a, wk_a, act_a;
    conn_pool pool;
    conn_ui ui;
    conn_worker wk;
    conn_actions acts;
    int owner[SIM_MAX_ARENAS];
    sim_queue to_worker, to_ui;
    /* What the worker's glue was told, for the tests to assert. */
    conn_action wlog[SIM_LOG];
    size_t wlog_n;
    conn_action ulog[SIM_LOG];
    size_t ulog_n;
    uint64_t chunk_rows[SIM_MAX_ARENAS]; /* rows the glue stored in each chunk */
    bool waiting;  /* the worker glue holds a pq event until the next message */
    bool exited;
    size_t refusals;
    conn_refuse last_refusal;
} sim;

static inline void sim_push(sim_queue *q, size_t arena, proto_header h) {
    assert(q->tail - q->head < SIM_QUEUE);
    q->items[q->tail % SIM_QUEUE].arena = arena;
    q->items[q->tail % SIM_QUEUE].h = h;
    q->tail++;
}

static inline bool sim_pop(sim_queue *q, sim_msg *m) {
    if (q->head == q->tail) {
        return false;
    }
    *m = q->items[q->head % SIM_QUEUE];
    q->head++;
    return true;
}

/* The UI's book and the simulation agree on every arena. */
static inline void sim_check(sim *s) {
    size_t free = 0;
    for (size_t i = 0; i < s->pool.count; i++) {
        pool_owner o = conn_pool_owner(&s->pool, i);
        int so = s->owner[i];
        if (o == POOL_FREE) {
            assert(so == S_FREE);
            free++;
        } else if (o == POOL_UI) {
            assert(so == S_UI);
        } else {
            assert(so == S_TO_WORKER || so == S_WORKER || so == S_TO_UI);
        }
    }
    assert(free == s->pool.free);
}

static inline void sim_init(sim *s, size_t arenas, conn_limits lim) {
    memset(s, 0, sizeof *s);
    assert(arenas <= SIM_MAX_ARENAS);
    sc_arena_init(&s->book_a, s->book_mem, sizeof s->book_mem);
    sc_arena_init(&s->ui_a, s->ui_mem, sizeof s->ui_mem);
    sc_arena_init(&s->wk_a, s->wk_mem, sizeof s->wk_mem);
    sc_arena_init(&s->act_a, s->act_mem, sizeof s->act_mem);
    assert(conn_pool_init(&s->pool, &s->book_a, arenas));
    assert(conn_ui_init(&s->ui, &s->ui_a, 1, lim));
    assert(conn_worker_init(&s->wk, &s->wk_a, 1, lim.quota + 1));
    assert(conn_actions_init(&s->acts, &s->act_a, SIM_MAX_ARENAS + 16));
}

/* Performs the UI's actions in order. The UI takes arenas from the pool with
 * no action, so a SEND may carry one the simulation still shows as free, and
 * arenas it took and kept (the control arena) are marked after the replay. */
static inline bool sim_ui_perform(sim *s, bool ok) {
    for (size_t i = 0; i < s->acts.count; i++) {
        conn_action a = conn_action_at(&s->acts, i);
        assert(s->ulog_n < SIM_LOG);
        s->ulog[s->ulog_n++] = a;
        if (a.kind == ACT_SEND) {
            assert(s->owner[a.arena] == S_UI || s->owner[a.arena] == S_FREE);
            assert(proto_to_worker(a.msg));
            s->owner[a.arena] = S_TO_WORKER;
            proto_header h = {a.msg, a.conn, a.req, a.arg, a.aux, 0};
            sim_push(&s->to_worker, a.arena, h);
        } else if (a.kind == ACT_FREE || a.kind == ACT_DROP) {
            /* The book already moved it; the arena was the UI's before. */
            assert(s->owner[a.arena] == S_UI);
            s->owner[a.arena] = S_FREE;
        } else if (a.kind == ACT_RESET) {
            assert(s->owner[a.arena] == S_UI);
        } else if (a.kind == ACT_REFUSE) {
            s->refusals++;
            s->last_refusal = (conn_refuse)a.aux;
        } else {
            assert(0 && "worker action from the UI machine");
        }
    }
    conn_actions_clear(&s->acts);
    for (size_t i = 0; i < s->pool.count; i++) {
        if (conn_pool_owner(&s->pool, i) == POOL_UI && s->owner[i] == S_FREE) {
            s->owner[i] = S_UI;
        }
    }
    sim_check(s);
    return ok;
}

static inline bool sim_ui(sim *s, conn_ui_event_kind k) {
    SIM_T("ui cmd %d (conn %d q %d)\n", (int)k, (int)s->ui.state, (int)s->ui.q);
    conn_ui_event e = {k, 0, {PROTO_NONE, 0, 0, 0, 0, 0}};
    bool ok = conn_ui_step(&s->ui, &s->pool, e, &s->acts);
    return sim_ui_perform(s, ok);
}

/* Hands the UI a message as if a worker sent it in `arena`, which the
 * simulation must show as with the worker. */
static inline bool sim_inject(sim *s, size_t arena, proto_kind k, uint64_t req, uint64_t arg,
                       uint64_t aux) {
    assert(s->owner[arena] == S_WORKER || s->owner[arena] == S_TO_WORKER);
    s->owner[arena] = S_UI;
    conn_ui_event e = {CONN_UI_EV_MSG, arena, {k, s->ui.conn, req, arg, aux, 0}};
    bool ok = conn_ui_step(&s->ui, &s->pool, e, &s->acts);
    return sim_ui_perform(s, ok);
}

static inline bool sim_worker_perform(sim *s, bool ok) {
    for (size_t i = 0; i < s->acts.count; i++) {
        conn_action a = conn_action_at(&s->acts, i);
        assert(s->wlog_n < SIM_LOG);
        s->wlog[s->wlog_n++] = a;
        if (a.kind == ACT_SEND) {
            assert(s->owner[a.arena] == S_WORKER);
            assert(!proto_to_worker(a.msg));
            s->owner[a.arena] = S_TO_UI;
            proto_header h = {a.msg, a.conn, a.req, a.arg, a.aux, 0};
            sim_push(&s->to_ui, a.arena, h);
            if (a.msg == PROTO_CHUNK) {
                assert(a.arg == s->chunk_rows[a.arena]);
            }
        } else if (a.kind == ACT_STORE_ROW) {
            assert(s->owner[a.arena] == S_WORKER);
            if (a.arg == 1) {
                s->chunk_rows[a.arena] = 0;
            }
        } else if (a.kind == ACT_WAIT) {
            s->waiting = true;
        } else if (a.kind == ACT_EXIT) {
            s->exited = true;
        } else {
            assert(a.kind == ACT_CONNECT || a.kind == ACT_QUERY || a.kind == ACT_DISCARD ||
                   a.kind == ACT_CANCEL || a.kind == ACT_FINISH);
        }
    }
    conn_actions_clear(&s->acts);
    sim_check(s);
    return ok;
}

/* A libpq event for the worker. */
static inline bool sim_pq(sim *s, conn_worker_event_kind k, uint64_t aux, uint64_t now) {
    conn_worker_event e = {k, 0, {PROTO_NONE, 0, 0, 0, 0, 0}, aux, now};
    SIM_T("pq %d aux %llu (w %d)\n", (int)k, (unsigned long long)aux, (int)s->wk.state);
    s->waiting = false;
    bool ok = conn_worker_step(&s->wk, e, &s->acts);
    return sim_worker_perform(s, ok);
}

/* A row arrives and the glue stores it, sealing chunks that fill after
 * `per_chunk` rows. Returns the action the worker answered with. */
static inline act_kind sim_row(sim *s, uint64_t per_chunk, uint64_t now) {
    for (;;) {
        size_t before = s->wlog_n;
        assert(sim_pq(s, W_EV_ROW, 0, now));
        assert(s->wlog_n > before);
        conn_action a = s->wlog[s->wlog_n - 1];
        if (a.kind != ACT_STORE_ROW) {
            return a.kind;
        }
        if (s->chunk_rows[a.arena] >= per_chunk) {
            assert(sim_pq(s, W_EV_CHUNK_FULL, 0, now));
            continue;
        }
        s->chunk_rows[a.arena]++;
        assert(sim_pq(s, W_EV_ROW_STORED, 0, now));
        return ACT_STORE_ROW;
    }
}

/* Delivers one queued message to the worker. */
static inline bool sim_to_worker(sim *s) {
    sim_msg m;
    if (!sim_pop(&s->to_worker, &m)) {
        return false;
    }
    /* The worker thread has returned: a message now would strand its arena. */
    assert(!s->exited);
    assert(s->owner[m.arena] == S_TO_WORKER);
    s->owner[m.arena] = S_WORKER;
    SIM_T("-> worker kind %d arena %zu req %llu aux %llu (w %d)\n", (int)m.h.kind, m.arena,
          (unsigned long long)m.h.req, (unsigned long long)m.h.aux, (int)s->wk.state);
    conn_worker_event e = {W_EV_MSG, m.arena, m.h, 0, 0};
    s->waiting = false;
    assert(sim_worker_perform(s, conn_worker_step(&s->wk, e, &s->acts)));
    return true;
}

/* Delivers one queued message to the UI. */
static inline bool sim_to_ui(sim *s) {
    sim_msg m;
    if (!sim_pop(&s->to_ui, &m)) {
        return false;
    }
    assert(s->owner[m.arena] == S_TO_UI);
    s->owner[m.arena] = S_UI;
    SIM_T("-> ui kind %d arena %zu req %llu arg %llu (conn %d q %d out %zu)\n", (int)m.h.kind,
          m.arena, (unsigned long long)m.h.req, (unsigned long long)m.h.arg, (int)s->ui.state,
          (int)s->ui.q, s->ui.outstanding);
    conn_ui_event e = {CONN_UI_EV_MSG, m.arena, m.h};
    assert(sim_ui_perform(s, conn_ui_step(&s->ui, &s->pool, e, &s->acts)));
    return true;
}

/* Delivers until both queues are empty. */
static inline void sim_settle(sim *s) {
    while (sim_to_worker(s) || sim_to_ui(s)) {
    }
}

/* The last action the worker took of kind `k`, or one of ACT_NONE. */
static inline conn_action sim_last(const conn_action *log, size_t n, act_kind k, proto_kind msg) {
    conn_action none = {ACT_NONE, PROTO_NONE, 0, 0, 0, 0, 0};
    for (size_t i = n; i > 0; i--) {
        if (log[i - 1].kind == k && (msg == PROTO_NONE || log[i - 1].msg == msg)) {
            return log[i - 1];
        }
    }
    return none;
}

static inline size_t sim_count(const conn_action *log, size_t from, size_t n, act_kind k,
                        proto_kind msg) {
    size_t c = 0;
    for (size_t i = from; i < n; i++) {
        if (log[i].kind == k && (msg == PROTO_NONE || log[i].msg == msg)) {
            c++;
        }
    }
    return c;
}

/* Opens and connects. */
static inline void sim_connect(sim *s) {
    assert(sim_ui(s, CONN_UI_EV_OPEN));
    sim_settle(s);
    assert(sim_pq(s, W_EV_CONNECTED, 0, 0));
    sim_settle(s);
    assert(s->ui.state == UI_CONN_CONNECTED);
    assert(s->wk.state == W_READY);
}

/* Quits and drains: the worker exited, and every arena is back in the pool
 * except those the last result still holds. */
static inline void sim_close(sim *s) {
    assert(sim_ui(s, CONN_UI_EV_CLOSE));
    sim_settle(s);
    assert(s->exited);
    assert(s->ui.state == UI_CONN_CLOSED);
    assert(s->to_worker.head == s->to_worker.tail && s->to_ui.head == s->to_ui.tail);
    size_t held = s->ui.held_count + (s->ui.terminal_kept ? 1u : 0u);
    assert(s->pool.free + held == s->pool.count);
}

#endif
