#include "conn.h"

/* ---- Pool book ---- */

bool conn_pool_init(conn_pool *p, sc_arena *a, budget *b) {
    size_t count = b->count;
    p->arena = a;
    p->budget = b;
    p->count = 0;
    p->free = 0;
    p->retiring = 0;
    p->owner = SC_TSLICE_ALLOC(uint8_t, a, count);
    p->conn = SC_TSLICE_ALLOC(uint32_t, a, count);
    if (count == 0 || sc_tslice_len(p->owner) != count || sc_tslice_len(p->conn) != count) {
        return false;
    }
    uint8_t v = (uint8_t)POOL_FREE;
    uint32_t none = 0;
    for (size_t i = 0; i < count; i++) {
        if (!SC_TSLICE_SET(uint8_t, a, p->owner, i, &v) ||
            !SC_TSLICE_SET(uint32_t, a, p->conn, i, &none)) {
            return false;
        }
    }
    p->count = count;
    p->free = count;
    return true;
}

pool_owner conn_pool_owner(const conn_pool *p, size_t i) {
    uint8_t v = (uint8_t)POOL_FREE;
    if (i >= p->count || !SC_TSLICE_GET(uint8_t, p->arena, p->owner, i, &v)) {
        return POOL_FREE;
    }
    return (pool_owner)v;
}

static bool pool_move(conn_pool *p, size_t i, pool_owner from, pool_owner to) {
    if (i >= p->count || conn_pool_owner(p, i) != from) {
        return false;
    }
    if (to == POOL_FREE && !budget_put(p->budget, i)) {
        return false;
    }
    uint8_t v = (uint8_t)to;
    if (!SC_TSLICE_SET(uint8_t, p->arena, p->owner, i, &v)) {
        return false;
    }
    if (from == POOL_FREE) {
        p->free--;
    }
    if (to == POOL_FREE) {
        p->free++;
    }
    return true;
}

/* The connection that took arena `i`, or 0 for none. */
static uint32_t pool_conn(const conn_pool *p, size_t i) {
    uint32_t v = 0;
    return i < p->count && SC_TSLICE_GET(uint32_t, p->arena, p->conn, i, &v) ? v : 0;
}

/* A free arena for connection `conn`, charged to `owner`. */
static size_t pool_take(conn_pool *p, uint32_t conn, budget_owner owner, budget_use use,
                        size_t set, size_t out) {
    budget_class c = budget_pick(p->budget, owner, use, set, out);
    for (size_t i = 0; i < p->count && p->free > 0 && c != BUDGET_FROM_NONE; i++) {
        if (conn_pool_owner(p, i) == POOL_FREE && budget_take(p->budget, i, owner, c)) {
            return pool_move(p, i, POOL_FREE, POOL_UI) &&
                           SC_TSLICE_SET(uint32_t, p->arena, p->conn, i, &conn)
                       ? i
                       : SIZE_MAX;
        }
    }
    return SIZE_MAX;
}

/* Whether arena `i` is out for connection `conn`: with the UI or its worker,
 * not kept. */
static bool pool_of(const conn_pool *p, size_t i, uint32_t conn) {
    pool_owner o = conn_pool_owner(p, i);
    return (o == POOL_UI || o == POOL_WORKER) && pool_conn(p, i) == conn;
}

bool conn_pool_free_kept(conn_pool *p, size_t i) {
    return pool_move(p, i, POOL_KEPT, POOL_FREE);
}

bool conn_pool_can_retire(const conn_pool *p) {
    return p->retiring < BUDGET_RETIRING_SLOTS;
}

/* ---- Actions ---- */

bool conn_actions_init(conn_actions *out, sc_arena *a, size_t cap) {
    out->arena = a;
    out->count = 0;
    out->failed = false;
    out->items = SC_TSLICE_ALLOC(conn_action, a, cap);
    if (cap == 0 || sc_tslice_len(out->items) != cap) {
        out->failed = true;
        return false;
    }
    return true;
}

void conn_actions_clear(conn_actions *out) {
    out->count = 0;
    out->failed = false;
}

conn_action conn_action_at(const conn_actions *out, size_t i) {
    conn_action a = {ACT_NONE, PROTO_NONE, 0, 0, 0, 0, 0};
    if (i < out->count) {
        (void)SC_TSLICE_GET(conn_action, out->arena, out->items, i, &a);
    }
    return a;
}

bool conn_emit(conn_actions *out, act_kind k, proto_kind msg, size_t arena, uint32_t conn,
               uint64_t req, uint64_t arg, uint64_t aux) {
    conn_action a = {k, msg, arena, conn, req, arg, aux};
    if (out->failed || out->count >= sc_tslice_len(out->items) ||
        !SC_TSLICE_SET(conn_action, out->arena, out->items, out->count, &a)) {
        out->failed = true;
        return false;
    }
    out->count++;
    return true;
}

static bool act(conn_actions *out, act_kind k, size_t arena) {
    return conn_emit(out, k, PROTO_NONE, arena, 0, 0, 0, 0);
}

/* ---- UI side ---- */

static bool ui_send(conn_ui *c, conn_pool *p, conn_actions *out, proto_kind k, size_t arena,
                    uint64_t arg, uint64_t aux) {
    return pool_move(p, arena, POOL_UI, POOL_WORKER) &&
           conn_emit(out, ACT_SEND, k, arena, c->conn, c->req, arg, aux);
}

static bool ui_refuse(conn_actions *out, conn_refuse why) {
    return conn_emit(out, ACT_REFUSE, PROTO_NONE, 0, 0, 0, 0, (uint64_t)why);
}

/* Frees an arena the UI holds back to the pool. */
static bool ui_free(conn_pool *p, conn_actions *out, act_kind k, size_t arena) {
    return pool_move(p, arena, POOL_UI, POOL_FREE) && act(out, k, arena);
}

bool conn_ui_init(conn_ui *c, sc_arena *book, uint32_t conn, budget_owner owner,
                  conn_limits lim) {
    conn_ui z = {0};
    *c = z;
    c->conn = conn;
    c->owner = owner;
    c->lim = lim;
    c->book = book;
    c->next_req = 1;
    c->run_limit = lim.row_limit;
    c->held = SC_TSLICE_ALLOC(size_t, book, lim.quota);
    return lim.window > 0 && lim.quota > 0 && lim.row_limit > 0 &&
           sc_tslice_len(c->held) == lim.quota;
}

size_t conn_ui_held(const conn_ui *c, size_t i) {
    size_t v = SIZE_MAX;
    if (i >= c->held_count || !SC_TSLICE_GET(size_t, c->book, c->held, i, &v)) {
        return SIZE_MAX;
    }
    return v;
}

static bool q_active(query_state q) {
    return q == Q_RUNNING || q == Q_STREAMING || q == Q_CANCELLING;
}

bool conn_ui_active(const conn_ui *c) {
    return c->q == Q_QUEUED || q_active(c->q);
}

/* Frees the current set's arenas. */
static bool ui_free_set(conn_ui *c, conn_pool *p, conn_actions *out) {
    for (size_t i = 0; i < c->held_count; i++) {
        if (!ui_free(p, out, ACT_FREE, conn_ui_held(c, i))) {
            return false;
        }
    }
    c->held_count = 0;
    c->rows = 0;
    return true;
}

/* Sends CANCEL or CLOSE in the control arena, or marks it pending. */
static bool ui_control(conn_ui *c, conn_pool *p, conn_actions *out, proto_kind k, proto_cap why) {
    if (k == PROTO_CANCEL && c->state == UI_CONN_CLOSING) {
        return true; /* CLOSE ends the query; the worker exits after it */
    }
    if (!c->ctl_home) {
        if (k == PROTO_CLOSE) {
            c->close_pending = true;
        } else {
            c->cancel_pending = true;
            c->cancel_why = why;
        }
        return true;
    }
    c->ctl_home = false;
    if (k == PROTO_CANCEL) {
        c->cancel_sent = true;
        c->cancel_pending = false;
    }
    return ui_send(c, p, out, k, c->ctl, 0, k == PROTO_CANCEL ? (uint64_t)why : 0);
}

/* Tops the worker's credit up to the window. The current set may hold `quota`
 * arenas, and one credit past that stays with the worker so the next
 * statement's COLUMNS always has an arena (KTD12); the worker never stores
 * rows in it. With credit wanted, none in flight and the pool dry, the UI
 * cancels: for quota when the set is full, else for memory (KTD5). */
static bool ui_grant(conn_ui *c, conn_pool *p, conn_actions *out) {
    while (c->outstanding < c->lim.window && c->held_count + c->outstanding <= c->lim.quota) {
        size_t g = pool_take(p, c->conn, c->owner, BUDGET_USE_GRANT,
                             c->held_count + c->outstanding, c->outstanding);
        if (g == SIZE_MAX) {
            if (c->outstanding > 0) {
                return true;
            }
            c->cap = c->held_count >= c->lim.quota ? PROTO_CAP_QUOTA : PROTO_CAP_MEMORY;
            return ui_control(c, p, out, PROTO_CANCEL, c->cap);
        }
        c->outstanding++;
        if (!ui_send(c, p, out, PROTO_GRANT, g, 0, 0)) {
            return false;
        }
    }
    return true;
}

static bool ui_start_run(conn_ui *c, conn_pool *p, conn_actions *out) {
    /* Refuse before freeing anything, so a refused run keeps the old result. */
    size_t reclaim = c->held_count + (c->terminal_kept ? 1u : 0u);
    if (budget_room(p->budget, c->owner) + reclaim < 1u + BUDGET_FLOOR_GRANTS) {
        if (c->q == Q_QUEUED) {
            c->q = Q_NONE;
        }
        return ui_refuse(out, CONN_REFUSE_POOL);
    }
    c->q = Q_NONE;
    if (c->terminal_kept) {
        c->terminal_kept = false;
        if (!ui_free(p, out, ACT_FREE, c->terminal)) {
            return false;
        }
    }
    if (!ui_free_set(c, p, out)) {
        return false;
    }
    c->terminal = pool_take(p, c->conn, c->owner, BUDGET_USE_RUN, 0, 0);
    if (c->terminal == SIZE_MAX) {
        return false;
    }
    c->req = c->next_req++;
    c->terminal_out = true;
    c->outstanding = 0;
    c->sets = 0;
    c->earlier_tags = 0;
    c->cap = PROTO_CAP_USER;
    c->cancel_sent = false;
    c->cancel_pending = false;
    c->abandoned = false;
    c->q = Q_RUNNING;
    return ui_send(c, p, out, PROTO_RUN, c->terminal, c->run_limit, (uint64_t)c->lim.quota) &&
           ui_grant(c, p, out);
}

static bool conn_ui_open(conn_ui *c, conn_pool *p, conn_actions *out) {
    if (c->state != UI_CONN_NONE) {
        return ui_refuse(out, CONN_REFUSE_BUSY);
    }
    if (budget_spare(p->budget, BUDGET_FROM_CONTROL) < 2) {
        return ui_refuse(out, CONN_REFUSE_POOL);
    }
    c->ctl = pool_take(p, c->conn, c->owner, BUDGET_USE_CONTROL, 0, 0);
    c->wrk = pool_take(p, c->conn, c->owner, BUDGET_USE_CONTROL, 0, 0);
    c->reserved = true;
    c->ctl_home = true;
    c->state = UI_CONN_CONNECTING;
    return ui_send(c, p, out, PROTO_OPEN, c->wrk, 0, 0);
}

static bool ui_run(conn_ui *c, conn_pool *p, conn_actions *out, uint64_t limit) {
    if (q_active(c->q) || c->q == Q_QUEUED) {
        return ui_refuse(out, CONN_REFUSE_BUSY);
    }
    c->run_limit = limit > 0 ? limit : c->lim.row_limit;
    if (c->state == UI_CONN_CONNECTING) {
        c->q = Q_QUEUED;
        return true;
    }
    if (c->state != UI_CONN_CONNECTED) {
        return ui_refuse(out, CONN_REFUSE_NOT_CONNECTED);
    }
    return ui_start_run(c, p, out);
}

static bool ui_cancel(conn_ui *c, conn_pool *p, conn_actions *out, uint64_t now_ms) {
    if (c->q == Q_QUEUED) {
        c->q = Q_CANCELLED;
        return true;
    }
    if (c->q != Q_RUNNING && c->q != Q_STREAMING) {
        return true;
    }
    c->q = Q_CANCELLING;
    c->cancel_ms = now_ms;
    c->offer_disconnect = false;
    c->cancel_failed = false;
    if (c->cancel_sent || c->cancel_pending) {
        return true;
    }
    return ui_control(c, p, out, PROTO_CANCEL, PROTO_CAP_USER);
}

static bool conn_ui_close(conn_ui *c, conn_pool *p, conn_actions *out) {
    if (c->state == UI_CONN_NONE) {
        c->state = UI_CONN_CLOSED;
        return true;
    }
    if (c->state == UI_CONN_CLOSING || c->state == UI_CONN_CLOSED) {
        return true;
    }
    if (c->q == Q_QUEUED) {
        c->q = Q_CANCELLED;
    }
    c->state = UI_CONN_CLOSING;
    return ui_control(c, p, out, PROTO_CLOSE, PROTO_CAP_USER);
}

static bool ui_reserved_arena(const conn_ui *c, size_t arena) {
    return c->reserved && (arena == c->ctl || arena == c->wrk);
}

static void ui_unretire(conn_ui *c, conn_pool *p) {
    if (c->retiring) {
        c->retiring = false;
        p->retiring--;
    }
}

/* A reply that is not for the live request: back to the pool, unless it is a
 * reserved arena, which no stale reply can travel in. */
static bool ui_drop(conn_ui *c, conn_pool *p, conn_actions *out, size_t arena) {
    if (ui_reserved_arena(c, arena) || (c->terminal_out && arena == c->terminal)) {
        return false;
    }
    return ui_free(p, out, ACT_DROP, arena);
}

/* The worker's reserved arena came back with a connection reply. */
static bool ui_conn_reply(conn_ui *c, conn_pool *p, conn_actions *out, conn_ui_event e) {
    if (!c->reserved || e.arena != c->wrk) {
        return false;
    }
    if (e.h.kind == PROTO_BYE) {
        c->state = UI_CONN_CLOSED;
        c->reserved = false;
        ui_unretire(c, p);
        if (q_active(c->q) || c->q == Q_QUEUED) {
            c->q = Q_DISCONNECTED;
        }
        bool ok = ui_free(p, out, ACT_FREE, c->wrk);
        if (!c->ctl_home || c->outstanding > 0 || c->terminal_out) {
            return false; /* the worker returns every arena before BYE */
        }
        return ui_free(p, out, ACT_FREE, c->ctl) && ok;
    }
    bool closing = c->state == UI_CONN_CLOSING;
    if (e.h.kind == PROTO_CANCEL_FAILED) {
        if (e.h.req == c->req && c->q == Q_CANCELLING) {
            c->cancel_failed = true;
            c->offer_disconnect = true;
        }
    } else if (e.h.kind == PROTO_CONNECTED) {
        if (c->state != UI_CONN_CONNECTING && !closing) {
            return false;
        }
        if (!closing) {
            c->state = UI_CONN_CONNECTED;
        }
    } else if (e.h.kind == PROTO_CONN_FAILED) {
        if (!closing) {
            c->state = e.h.aux == (uint64_t)PROTO_FAIL_AUTH ? UI_CONN_AUTH_FAILED
                                                            : UI_CONN_UNREACHABLE;
        }
    } else {
        if (!closing) {
            c->state = UI_CONN_DISCONNECTED;
        }
        if (q_active(c->q) || c->q == Q_QUEUED) {
            c->q = Q_DISCONNECTED;
        }
    }
    if (!ui_send(c, p, out, PROTO_RETURN_TO_WORKER, c->wrk, 0, 0)) {
        return false;
    }
    if (c->state == UI_CONN_CONNECTED && c->q == Q_QUEUED) {
        return ui_start_run(c, p, out);
    }
    return true;
}

static bool ui_return(conn_ui *c, conn_pool *p, conn_actions *out, conn_ui_event e) {
    if (c->reserved && e.arena == c->wrk) {
        return false; /* the worker's arena only carries connection replies */
    }
    if (c->reserved && e.arena == c->ctl) {
        if (c->ctl_home) {
            return false;
        }
        c->ctl_home = true;
        if (!act(out, ACT_RESET, c->ctl)) {
            return false;
        }
        if (c->close_pending) {
            c->close_pending = false;
            return ui_control(c, p, out, PROTO_CLOSE, PROTO_CAP_USER);
        }
        if (c->cancel_pending && (q_active(c->q))) {
            return ui_control(c, p, out, PROTO_CANCEL, c->cancel_why);
        }
        c->cancel_pending = false;
        return true;
    }
    if (e.h.req == c->req && c->terminal_out && e.arena == c->terminal) {
        c->terminal_out = false;
        return ui_free(p, out, ACT_FREE, e.arena);
    }
    if (e.h.req == c->req && c->outstanding > 0) {
        c->outstanding--;
        return ui_free(p, out, ACT_FREE, e.arena);
    }
    return ui_drop(c, p, out, e.arena);
}

/* The current set and a kept terminal reply become POOL_KEPT. */
static bool ui_pass_on(conn_ui *c, conn_pool *p) {
    for (size_t i = 0; i < c->held_count; i++) {
        if (!pool_move(p, conn_ui_held(c, i), POOL_UI, POOL_KEPT)) {
            return false;
        }
    }
    c->held_count = 0;
    if (c->terminal_kept) {
        c->terminal_kept = false;
        return pool_move(p, c->terminal, POOL_UI, POOL_KEPT);
    }
    return true;
}

static bool ui_keep(conn_ui *c, conn_pool *p) {
    return !conn_ui_active(c) && ui_pass_on(c, p);
}

static bool ui_retire(conn_ui *c, conn_pool *p, conn_actions *out) {
    if (c->retiring) {
        return true;
    }
    bool worker = c->state != UI_CONN_NONE && c->state != UI_CONN_CLOSED;
    if (worker && !conn_pool_can_retire(p)) {
        return ui_refuse(out, CONN_REFUSE_RETIRING);
    }
    if (conn_ui_active(c)) {
        c->q = Q_DISCONNECTED;
    }
    c->cancel_pending = false;
    if (!ui_pass_on(c, p)) {
        return false;
    }
    if (!worker) {
        return true;
    }
    for (size_t i = 0; i < p->count; i++) {
        if (pool_of(p, i, c->conn) && !budget_retire(p->budget, i)) {
            return false;
        }
    }
    c->owner = BUDGET_RETIRING;
    c->retiring = true;
    p->retiring++;
    if (c->state == UI_CONN_CLOSING) {
        return true;
    }
    c->state = UI_CONN_CLOSING;
    return ui_control(c, p, out, PROTO_CLOSE, PROTO_CAP_USER);
}

static bool ui_reclaim(conn_ui *c, conn_pool *p, conn_actions *out) {
    for (size_t i = 0; i < p->count; i++) {
        pool_owner o = conn_pool_owner(p, i);
        if (pool_of(p, i, c->conn) && !(pool_move(p, i, o, POOL_FREE) && act(out, ACT_FREE, i))) {
            return false;
        }
    }
    ui_unretire(c, p);
    if (conn_ui_active(c)) {
        c->q = Q_DISCONNECTED;
    }
    c->held_count = 0;
    c->terminal_kept = false;
    c->terminal_out = false;
    c->outstanding = 0;
    c->reserved = false;
    c->ctl_home = false;
    c->state = UI_CONN_CLOSED;
    return true;
}

/* A run Cancelling long enough offers Disconnect; at the deadline its cancel
 * has failed. */
static void ui_tick(conn_ui *c, uint64_t now_ms) {
    if (c->q != Q_CANCELLING || c->abandoned || now_ms < c->cancel_ms) {
        return;
    }
    uint64_t waited = now_ms - c->cancel_ms;
    c->offer_disconnect = c->offer_disconnect || waited >= CONN_DISCONNECT_OFFER_MS;
    c->cancel_failed = c->cancel_failed || waited >= CONN_CANCEL_DEADLINE_MS;
}

static bool ui_abandon(conn_ui *c, conn_pool *p, conn_actions *out, uint64_t now_ms) {
    if (!ui_free_set(c, p, out)) {
        return false;
    }
    if (c->terminal_kept) {
        c->terminal_kept = false;
        if (!ui_free(p, out, ACT_FREE, c->terminal)) {
            return false;
        }
    }
    if (c->q == Q_QUEUED) {
        c->q = Q_NONE;
        return true;
    }
    if (!q_active(c->q)) {
        return true;
    }
    c->abandoned = true;
    return ui_cancel(c, p, out, now_ms);
}

static bool ui_hold(conn_ui *c, size_t arena) {
    if (c->held_count >= sc_tslice_len(c->held) ||
        !SC_TSLICE_SET(size_t, c->book, c->held, c->held_count, &arena)) {
        return false;
    }
    c->held_count++;
    return true;
}

/* A retired connection's arena on its way home: a grant the worker used or a
 * terminal reply nobody reads. False when it is neither. */
static bool ui_retired_home(conn_ui *c, conn_pool *p, conn_actions *out, conn_ui_event e) {
    if (!c->retiring || e.h.req != c->req || ui_reserved_arena(c, e.arena)) {
        return false;
    }
    if (c->terminal_out && e.arena == c->terminal) {
        c->terminal_out = false;
    } else if (c->outstanding > 0) {
        c->outstanding--;
    } else {
        return false;
    }
    return ui_free(p, out, ACT_DROP, e.arena);
}

static bool ui_data(conn_ui *c, conn_pool *p, conn_actions *out, conn_ui_event e) {
    if (c->retiring) {
        return ui_retired_home(c, p, out, e) || ui_drop(c, p, out, e.arena);
    }
    if (e.h.req != c->req || !q_active(c->q) || c->outstanding == 0 ||
        ui_reserved_arena(c, e.arena) || (c->terminal_out && e.arena == c->terminal)) {
        return ui_drop(c, p, out, e.arena) && e.h.req != c->req;
    }
    c->outstanding--;
    if (c->abandoned) {
        return ui_free(p, out, ACT_FREE, e.arena); /* nobody reads it */
    }
    if (e.h.kind == PROTO_COLUMNS) {
        if (c->sets > 0 && !ui_free_set(c, p, out)) {
            return false;
        }
        c->sets++;
        if (c->q == Q_RUNNING) {
            c->q = Q_STREAMING;
        }
    } else if (c->sets == 0) {
        (void)ui_drop(c, p, out, e.arena); /* a CHUNK before its COLUMNS */
        return false;
    } else {
        c->rows += e.h.arg;
    }
    if (!ui_hold(c, e.arena)) {
        return false;
    }
    /* Once CLOSE is out the worker may already have exited: grant nothing. */
    if (c->q != Q_STREAMING || c->state != UI_CONN_CONNECTED || c->cancel_sent ||
        c->cancel_pending) {
        return true;
    }
    return ui_grant(c, p, out);
}

static bool ui_terminal(conn_ui *c, conn_pool *p, conn_actions *out, conn_ui_event e) {
    if (c->retiring) {
        return ui_retired_home(c, p, out, e) || ui_drop(c, p, out, e.arena);
    }
    if (e.h.req != c->req || !q_active(c->q) || !c->terminal_out || e.arena != c->terminal) {
        return ui_drop(c, p, out, e.arena) && e.h.req != c->req;
    }
    c->terminal_out = false;
    if (c->abandoned) {
        c->q = Q_CANCELLED;
        c->cancel_pending = false;
        return ui_free(p, out, ACT_FREE, e.arena);
    }
    c->terminal_kept = true;
    if (e.h.kind == PROTO_DONE) {
        c->earlier_tags = e.h.aux;
        c->q = c->sets > 0 ? Q_ROWS : Q_COMMAND_TAG;
    } else if (e.h.kind == PROTO_ERROR) {
        c->q = c->rows > 0 ? Q_ERROR_AFTER_ROWS : Q_ERROR;
    } else if (e.h.kind == PROTO_CAPPED) {
        c->cap = (proto_cap)e.h.aux;
        c->q = Q_CAPPED;
    } else {
        c->q = Q_CANCELLED;
    }
    c->cancel_pending = false;
    return true;
}

static bool ui_msg(conn_ui *c, conn_pool *p, conn_actions *out, conn_ui_event e) {
    if (!pool_move(p, e.arena, POOL_WORKER, POOL_UI)) {
        return false;
    }
    if (e.h.conn != c->conn || proto_to_worker(e.h.kind)) {
        (void)ui_drop(c, p, out, e.arena);
        return false;
    }
    switch (e.h.kind) {
    case PROTO_CONNECTED:
    case PROTO_CONN_FAILED:
    case PROTO_LOST:
    case PROTO_BYE:
    case PROTO_CANCEL_FAILED:
        return ui_conn_reply(c, p, out, e);
    case PROTO_RETURN:
        return ui_return(c, p, out, e);
    case PROTO_COLUMNS:
    case PROTO_CHUNK:
        return ui_data(c, p, out, e);
    default:
        return ui_terminal(c, p, out, e);
    }
}

bool conn_ui_step(conn_ui *c, conn_pool *p, conn_ui_event e, conn_actions *out) {
    bool ok = false;
    /* A closed connection still frees results: only commands are refused. */
    if (c->state == UI_CONN_CLOSED && (e.kind == CONN_UI_EV_OPEN || e.kind == CONN_UI_EV_RUN ||
                                       e.kind == CONN_UI_EV_CANCEL || e.kind == CONN_UI_EV_CLOSE)) {
        return e.kind == CONN_UI_EV_CLOSE || ui_refuse(out, CONN_REFUSE_NOT_CONNECTED);
    }
    switch (e.kind) {
    case CONN_UI_EV_OPEN:
        ok = conn_ui_open(c, p, out);
        break;
    case CONN_UI_EV_RUN:
        ok = ui_run(c, p, out, e.h.arg);
        break;
    case CONN_UI_EV_KEEP:
        ok = ui_keep(c, p);
        break;
    case CONN_UI_EV_ABANDON:
        ok = ui_abandon(c, p, out, e.now_ms);
        break;
    case CONN_UI_EV_CANCEL:
        ok = ui_cancel(c, p, out, e.now_ms);
        break;
    case CONN_UI_EV_RETIRE:
        ok = ui_retire(c, p, out);
        break;
    case CONN_UI_EV_TICK:
        ui_tick(c, e.now_ms);
        ok = true;
        break;
    case CONN_UI_EV_RECLAIM:
        ok = ui_reclaim(c, p, out);
        break;
    case CONN_UI_EV_CLOSE:
        ok = conn_ui_close(c, p, out);
        break;
    default:
        ok = ui_msg(c, p, out, e);
        break;
    }
    return ok && !out->failed;
}
