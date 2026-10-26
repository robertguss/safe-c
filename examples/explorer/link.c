#include "link.h"

/* The connection's private arenas, by index in `priv`. */
enum { PRIV_BOOK, PRIV_HANDLES, PRIV_WORKER_BOOK, PRIV_WORKER_HANDLES, PRIV_WIRES,
       PRIV_CONNINFO, PRIV_SQL, PRIV_FAILURE, PRIV_RESULT, PRIV_COUNT };

#define LINK_ACTIONS 64u
#define LINK_HANDLES 64u
#define LINK_STACK_BYTES (1u << 20)

static sc_arena *priv(const conn_link *l, size_t i) {
    return sc_mem_arena(&l->priv, i);
}

sc_arena *link_arena(const conn_link *l, size_t i) {
    return sc_mem_arena(&l->pool, i);
}

const sc_arena *link_failure_arena(const conn_link *l) {
    return priv(l, PRIV_FAILURE);
}

/* Copies `s` (live in `a`) into private arena `slot`, emptied first. */
static bool keep_text(conn_link *l, size_t slot, sc_str *out, const sc_arena *a, sc_str s) {
    sc_arena *dst = priv(l, slot);
    if (dst == NULL || !sc_mem_reset(&l->priv, slot)) {
        return false;
    }
    sc_str empty = {NULL, 0, dst->epoch};
    if (s.len == 0) {
        *out = empty;
        return true;
    }
    sc_buf b = sc_buf_new(sc_arena_alloc(dst, s.len));
    if (!sc_buf_str(dst, &b, a, s)) {
        return false;
    }
    *out = sc_buf_view(dst, b);
    return true;
}

static size_t pow2_at_least(size_t n) {
    size_t p = 1;
    while (p < n) {
        p *= 2;
    }
    return p;
}

bool link_init(conn_link *l, uint32_t conn, size_t arenas, conn_limits lim, worker_ring_fn ring,
               void *ring_arg) {
    conn_link z = {0};
    *l = z;
    l->wake.read_fd = -1;
    l->wake.write_fd = -1;
    if (!sc_mem_pool_new(&l->pool, arenas, LINK_ARENA_BYTES).ok) {
        return false;
    }
    if (!sc_mem_pool_new(&l->priv, PRIV_COUNT, LINK_TEXT_BYTES).ok) {
        (void)sc_mem_pool_free(&l->pool);
        return false;
    }
    sc_arena *book = priv(l, PRIV_BOOK);
    sc_arena *wires = priv(l, PRIV_WIRES);
    bool ok = conn_pool_init(&l->book, book, arenas) && conn_ui_init(&l->ui, book, conn, lim) &&
              conn_actions_init(&l->acts, book, LINK_ACTIONS) &&
              wire_side_init(&l->side, priv(l, PRIV_HANDLES), LINK_HANDLES) &&
              sc_chan_new(&l->requests, wires, arenas) &&
              sc_mbox_new(&l->replies, wires, pow2_at_least(arenas)) &&
              result_book_bytes(lim.quota) <= LINK_TEXT_BYTES &&
              result_init(&l->res, priv(l, PRIV_RESULT), lim.quota) && sc_wake_new(&l->wake).ok;
    l->wctx.conn = conn;
    l->wctx.pool = &l->pool;
    l->wctx.requests = &l->requests;
    l->wctx.replies = &l->replies;
    l->wctx.wake = l->wake;
    l->wctx.pending = &l->pending;
    l->wctx.ring = ring;
    l->wctx.ring_arg = ring_arg;
    l->wctx.book = priv(l, PRIV_WORKER_BOOK);
    l->wctx.handles = priv(l, PRIV_WORKER_HANDLES);
    l->wctx.max_grants = lim.window + lim.quota + 1u;
    l->broken = !ok;
    return ok;
}

/* Writes the message an action names into its arena and sends it. */
static bool send(conn_link *l, conn_action a) {
    proto_header h = {a.msg, a.conn, a.req, a.arg, a.aux, 0};
    proto_writer pw;
    sc_arena *ar = link_arena(l, a.arena);
    if (ar == NULL || !sc_mem_reset(&l->pool, a.arena) || !proto_begin(&pw, ar, h)) {
        return false;
    }
    if (a.msg == PROTO_OPEN && !proto_put_str(&pw, priv(l, PRIV_CONNINFO), l->conninfo)) {
        return false;
    }
    if (a.msg == PROTO_RUN && !proto_put_str(&pw, priv(l, PRIV_SQL), l->sql)) {
        return false;
    }
    if (!proto_end(&pw) || !wire_chan_send(&l->side, &l->requests, a.arena)) {
        return false;
    }
    (void)sc_wake_ring(l->wake);
    return true;
}

static bool perform(conn_link *l) {
    bool ok = true;
    for (size_t i = 0; i < l->acts.count && ok; i++) {
        conn_action a = conn_action_at(&l->acts, i);
        if (a.kind == ACT_SEND) {
            ok = send(l, a);
        } else if (a.kind == ACT_FREE || a.kind == ACT_DROP || a.kind == ACT_RESET) {
            ok = sc_mem_reset(&l->pool, a.arena);
        } else if (a.kind == ACT_REFUSE) {
            l->refused = true;
            l->refusal = (conn_refuse)a.aux;
        } else {
            ok = false;
        }
    }
    conn_actions_clear(&l->acts);
    return ok;
}

/* Brings `res` up to the set conn_ui holds: a new request or set replaces
 * it, and arenas held since the last step are added (COLUMNS first). */
static bool sync_result(conn_link *l) {
    const conn_ui *u = &l->ui;
    size_t have = (l->res.has_columns ? 1u : 0u) + l->res.count;
    if (u->req != l->res_req || u->sets != l->res_sets || u->held_count < have) {
        sc_tslice none = {{NULL, 0, 0}, 0};
        (void)result_close(&l->res, NULL, none);
        l->res_req = u->req;
        l->res_sets = u->sets;
        have = 0;
    }
    for (size_t i = have; i < u->held_count; i++) {
        size_t idx = conn_ui_held(u, i);
        const sc_arena *a = link_arena(l, idx);
        result_add got = i == 0 ? result_set_columns(&l->res, a, idx)
                                : result_add_chunk(&l->res, a, idx, l->res.rows);
        if (got != RESULT_ADDED) {
            return false;
        }
    }
    return true;
}

static bool step(conn_link *l, conn_ui_event e) {
    if (l->broken) {
        return false;
    }
    bool ok = conn_ui_step(&l->ui, &l->book, e, &l->acts);
    ok = perform(l) && ok;
    ok = sync_result(l) && ok;
    if (!ok) {
        l->broken = true;
    }
    return ok;
}

static bool command(conn_link *l, conn_ui_event_kind k) {
    conn_ui_event e = {k, 0, {PROTO_NONE, 0, 0, 0, 0, 0}};
    l->refused = false;
    return step(l, e);
}

bool link_open(conn_link *l, const sc_arena *a, sc_str conninfo) {
    if (l->broken || l->started || !keep_text(l, PRIV_CONNINFO, &l->conninfo, a, conninfo)) {
        return false;
    }
    if (!sc_thread_start(&l->thread, worker_main, &l->wctx, LINK_STACK_BYTES).ok) {
        return false;
    }
    l->started = true;
    return command(l, CONN_UI_EV_OPEN);
}

bool link_run(conn_link *l, const sc_arena *a, sc_str sql) {
    /* A run already active is refused before its SQL is replaced. */
    if (l->ui.q == Q_QUEUED || l->ui.q == Q_RUNNING || l->ui.q == Q_STREAMING ||
        l->ui.q == Q_CANCELLING) {
        return command(l, CONN_UI_EV_RUN);
    }
    /* Re-running the kept SQL: it is already in place, and copying it onto
     * itself would reset the arena it lives in first. */
    if (a == priv(l, PRIV_SQL)) {
        l->sql = sql;
    } else if (!keep_text(l, PRIV_SQL, &l->sql, a, sql)) {
        return false;
    }
    return command(l, CONN_UI_EV_RUN);
}

bool link_cancel(conn_link *l) {
    return command(l, CONN_UI_EV_CANCEL);
}

bool link_close(conn_link *l) {
    return command(l, CONN_UI_EV_CLOSE);
}

bool link_poll(conn_link *l) {
    /* Clear before draining, so a ring for a reply published during the drain
     * is not lost (KTD6). */
    sc_flag_clear(&l->pending);
    /* Read before the drain: a worker sets it after its last send. */
    bool done = sc_flag_is_set(&l->wctx.done);
    bool any = false;
    size_t i = 0;
    while (!l->broken && wire_mbox_recv(&l->side, &l->replies, &i) == SC_MBOX_OK) {
        any = true;
        sc_arena *ar = link_arena(l, i);
        proto_header h;
        proto_reader r;
        if (ar == NULL || !proto_read(ar, &h, &r)) {
            l->broken = true;
            break;
        }
        if (h.kind == PROTO_CONN_FAILED) {
            sc_str msg;
            if (!proto_get_str(&r, &msg) || !keep_text(l, PRIV_FAILURE, &l->failure, ar, msg)) {
                l->broken = true;
                break;
            }
        }
        conn_ui_event e = {CONN_UI_EV_MSG, i, h};
        (void)step(l, e);
    }
    if (done && l->ui.state != UI_CONN_CLOSED) {
        l->broken = true; /* the worker returned without BYE */
    }
    return any;
}

bool link_join(conn_link *l) {
    if (!l->started) {
        return false;
    }
    l->started = false;
    bool bye = l->ui.state == UI_CONN_CLOSED;
    if (!bye) {
        /* No BYE: OPEN was refused, the link broke, or the worker failed. A
         * closed request channel makes a live worker return. */
        sc_chan_close(&l->requests);
        (void)sc_wake_ring(l->wake);
    }
    return sc_thread_join(&l->thread).ok && !l->wctx.broken && bye;
}

void link_free(conn_link *l) {
    sc_wake_free(&l->wake);
    (void)sc_mem_pool_free(&l->priv);
    (void)sc_mem_pool_free(&l->pool);
}
