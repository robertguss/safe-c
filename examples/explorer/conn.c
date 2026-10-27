#include "conn.h"

/* ---- Pool book ---- */

bool conn_pool_init(conn_pool *p, sc_arena *a, budget *b) {
    size_t count = b->count;
    p->arena = a;
    p->budget = b;
    p->count = 0;
    p->free = 0;
    p->owner = SC_TSLICE_ALLOC(uint8_t, a, count);
    if (count == 0 || sc_tslice_len(p->owner) != count) {
        return false;
    }
    uint8_t v = (uint8_t)POOL_FREE;
    for (size_t i = 0; i < count; i++) {
        if (!SC_TSLICE_SET(uint8_t, a, p->owner, i, &v)) {
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

static size_t pool_take(conn_pool *p, budget_owner owner, budget_use use, size_t set,
                        size_t out) {
    budget_class c = budget_pick(p->budget, owner, use, set, out);
    for (size_t i = 0; i < p->count && p->free > 0 && c != BUDGET_FROM_NONE; i++) {
        if (conn_pool_owner(p, i) == POOL_FREE && budget_take(p->budget, i, owner, c)) {
            return pool_move(p, i, POOL_FREE, POOL_UI) ? i : SIZE_MAX;
        }
    }
    return SIZE_MAX;
}

bool conn_pool_free_kept(conn_pool *p, size_t i) {
    return pool_move(p, i, POOL_KEPT, POOL_FREE);
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

static bool emit(conn_actions *out, act_kind k, proto_kind msg, size_t arena, uint32_t conn,
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
    return emit(out, k, PROTO_NONE, arena, 0, 0, 0, 0);
}

/* ---- UI side ---- */

static bool ui_send(conn_ui *c, conn_pool *p, conn_actions *out, proto_kind k, size_t arena,
                    uint64_t arg, uint64_t aux) {
    return pool_move(p, arena, POOL_UI, POOL_WORKER) &&
           emit(out, ACT_SEND, k, arena, c->conn, c->req, arg, aux);
}

static bool ui_refuse(conn_actions *out, conn_refuse why) {
    return emit(out, ACT_REFUSE, PROTO_NONE, 0, 0, 0, 0, (uint64_t)why);
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
        size_t g = pool_take(p, c->owner, BUDGET_USE_GRANT, c->held_count + c->outstanding,
                             c->outstanding);
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
    if (budget_room(p->budget, c->owner) + reclaim < 2) {
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
    c->terminal = pool_take(p, c->owner, BUDGET_USE_RUN, 0, 0);
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
    c->ctl = pool_take(p, c->owner, BUDGET_USE_CONTROL, 0, 0);
    c->wrk = pool_take(p, c->owner, BUDGET_USE_CONTROL, 0, 0);
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

static bool ui_cancel(conn_ui *c, conn_pool *p, conn_actions *out) {
    if (c->q == Q_QUEUED) {
        c->q = Q_CANCELLED;
        return true;
    }
    if (c->q != Q_RUNNING && c->q != Q_STREAMING) {
        return true;
    }
    c->q = Q_CANCELLING;
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

/* A reply that is not for the live request: back to the pool, unless it is a
 * reserved arena, which no stale reply can travel in. */
static bool ui_drop(conn_ui *c, conn_pool *p, conn_actions *out, size_t arena) {
    if ((c->reserved && (arena == c->ctl || arena == c->wrk)) ||
        (c->terminal_out && arena == c->terminal)) {
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
    if (e.h.kind == PROTO_CONNECTED) {
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
        if (c->q == Q_QUEUED) {
            c->q = Q_NONE;
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

static bool ui_keep(conn_ui *c, conn_pool *p) {
    if (conn_ui_active(c)) {
        return false;
    }
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

static bool ui_abandon(conn_ui *c, conn_pool *p, conn_actions *out) {
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
    return ui_cancel(c, p, out);
}

static bool ui_hold(conn_ui *c, size_t arena) {
    if (c->held_count >= sc_tslice_len(c->held) ||
        !SC_TSLICE_SET(size_t, c->book, c->held, c->held_count, &arena)) {
        return false;
    }
    c->held_count++;
    return true;
}

static bool ui_data(conn_ui *c, conn_pool *p, conn_actions *out, conn_ui_event e) {
    if (e.h.req != c->req || !q_active(c->q) || c->outstanding == 0 ||
        (c->reserved && (e.arena == c->ctl || e.arena == c->wrk)) ||
        (c->terminal_out && e.arena == c->terminal)) {
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
        ok = ui_abandon(c, p, out);
        break;
    case CONN_UI_EV_CANCEL:
        ok = ui_cancel(c, p, out);
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

/* ---- Worker side ---- */

bool conn_worker_init(conn_worker *w, sc_arena *book, uint32_t conn, size_t max_grants) {
    conn_worker z = {0};
    *w = z;
    w->conn = conn;
    w->book = book;
    w->grants = SC_TSLICE_ALLOC(size_t, book, max_grants);
    return max_grants > 0 && sc_tslice_len(w->grants) == max_grants;
}

static bool w_send(conn_worker *w, conn_actions *out, proto_kind k, size_t arena, uint64_t req,
                   uint64_t arg, uint64_t aux) {
    return emit(out, ACT_SEND, k, arena, w->conn, req, arg, aux);
}

static bool w_act(conn_worker *w, conn_actions *out, act_kind k, size_t arena) {
    return emit(out, k, PROTO_NONE, arena, w->conn, w->req, 0, 0);
}

/* Sends LOST, CONNECTED, CONN_FAILED or BYE in the reserved arena, or marks it
 * pending until the UI hands the arena back. */
static bool w_reserved(conn_worker *w, conn_actions *out, proto_kind k, uint64_t aux) {
    if (!w->have_reserved) {
        if (k == PROTO_LOST) {
            w->lost_pending = true;
        } else if (k == PROTO_BYE) {
            w->bye_pending = true;
        } else {
            return false;
        }
        return true;
    }
    w->have_reserved = false;
    if (!w_send(w, out, k, w->reserved, 0, 0, aux)) {
        return false;
    }
    return k != PROTO_BYE || w_act(w, out, ACT_EXIT, 0);
}

static bool w_take(conn_worker *w, size_t *out) {
    if (w->grant_count == 0) {
        return false;
    }
    size_t g = 0;
    if (!SC_TSLICE_GET(size_t, w->book, w->grants, w->grant_count - 1, &g)) {
        return false;
    }
    w->grant_count--;
    *out = g;
    return true;
}

/* Sends the open chunk if it has rows, else hands it back unused. */
static bool w_flush(conn_worker *w, conn_actions *out) {
    if (!w->have_chunk) {
        return true;
    }
    w->have_chunk = false;
    if (w->chunk_rows == 0) {
        return w_send(w, out, PROTO_RETURN, w->chunk, w->req, 0, 0);
    }
    w->sent_in_set++;
    w->sent_chunk = true;
    return w_send(w, out, PROTO_CHUNK, w->chunk, w->req, w->chunk_rows, 0);
}

/* Returns every unused grant. */
static bool w_return_grants(conn_worker *w, conn_actions *out) {
    size_t g = 0;
    while (w_take(w, &g)) {
        if (!w_send(w, out, PROTO_RETURN, g, w->req, 0, 0)) {
            return false;
        }
    }
    return true;
}

static bool w_cancel(conn_worker *w, conn_actions *out, bool capped, proto_cap why) {
    w->capped = capped;
    w->cap = why;
    w->state = W_CANCELLING;
    return w_flush(w, out) && w_act(w, out, ACT_CANCEL, 0);
}

static bool w_cap(conn_worker *w, conn_actions *out, proto_cap why) {
    w->dropped = true;
    return w_cancel(w, out, true, why) && w_act(w, out, ACT_DISCARD, 0);
}

static bool w_busy(const conn_worker *w) {
    return w->state == W_RUNNING || w->state == W_CANCELLING || w->state == W_DRAINING;
}

/* The connection is gone: return the request's arenas, keeping its rows. */
static bool w_release(conn_worker *w, conn_actions *out) {
    if (!w_busy(w)) {
        return true;
    }
    return w_flush(w, out) && w_return_grants(w, out) &&
           w_send(w, out, PROTO_RETURN, w->terminal, w->req, 0, 0);
}

static bool w_msg(conn_worker *w, conn_worker_event e, conn_actions *out) {
    proto_kind k = e.h.kind;
    if (k == PROTO_OPEN) {
        if (w->state != W_IDLE) {
            return false;
        }
        w->reserved = e.arena;
        w->have_reserved = true;
        w->state = W_CONNECTING;
        return w_act(w, out, ACT_CONNECT, e.arena);
    }
    if (k == PROTO_RETURN_TO_WORKER) {
        if (w->have_reserved || e.arena != w->reserved) {
            return false;
        }
        w->have_reserved = true;
        if (w->lost_pending) {
            w->lost_pending = false;
            return w_reserved(w, out, PROTO_LOST, 0);
        }
        if (w->bye_pending) {
            w->bye_pending = false;
            return w_reserved(w, out, PROTO_BYE, 0);
        }
        return true;
    }
    if (k == PROTO_RUN) {
        if (w->state != W_READY) {
            return w_send(w, out, PROTO_RETURN, e.arena, e.h.req, 0, 0);
        }
        w->req = e.h.req;
        w->terminal = e.arena;
        w->row_limit = e.h.arg;
        w->quota = (size_t)e.h.aux;
        w->have_chunk = false;
        w->sent_chunk = false;
        w->sent_in_set = 0;
        w->rows = 0;
        w->tags = 0;
        w->in_set = false;
        w->capped = false;
        w->dropped = false;
        w->cap = PROTO_CAP_USER;
        w->state = W_RUNNING;
        return w_act(w, out, ACT_QUERY, e.arena);
    }
    if (k == PROTO_GRANT) {
        if (e.h.req != w->req || w->state != W_RUNNING) {
            return w_send(w, out, PROTO_RETURN, e.arena, e.h.req, 0, 0);
        }
        size_t g = e.arena;
        if (w->grant_count >= sc_tslice_len(w->grants) ||
            !SC_TSLICE_SET(size_t, w->book, w->grants, w->grant_count, &g)) {
            return false;
        }
        w->grant_count++;
        return true;
    }
    if (k == PROTO_CANCEL) {
        bool ok = true;
        if (e.h.req == w->req && w->state == W_RUNNING) {
            proto_cap why = (proto_cap)e.h.aux;
            ok = w_cancel(w, out, why != PROTO_CAP_USER, why);
        }
        return w_send(w, out, PROTO_RETURN, e.arena, e.h.req, 0, 0) && ok;
    }
    if (k == PROTO_CLOSE) {
        bool ok = w_release(w, out);
        if (w->state != W_IDLE && w->state != W_CLOSING && w->state != W_CLOSED) {
            ok = w_act(w, out, ACT_FINISH, 0) && ok;
        }
        ok = w_send(w, out, PROTO_RETURN, e.arena, e.h.req, 0, 0) && ok;
        w->state = W_CLOSED;
        w->lost_pending = false;
        return w_reserved(w, out, PROTO_BYE, 0) && ok;
    }
    return false;
}

/* A row is ready. */
static bool w_row(conn_worker *w, conn_actions *out) {
    if (w->state == W_CANCELLING || w->state == W_DRAINING) {
        w->dropped = true;
        return w_act(w, out, ACT_DISCARD, 0);
    }
    if (w->state != W_RUNNING || !w->in_set) {
        return false;
    }
    if (w->rows >= w->row_limit) {
        return w_cap(w, out, PROTO_CAP_LIMIT);
    }
    if (w->have_chunk) {
        return emit(out, ACT_STORE_ROW, PROTO_NONE, w->chunk, w->conn, w->req, 0, 0);
    }
    /* The set's quota counts its COLUMNS and CHUNKs; a credit past it is the
     * spare for the next statement's COLUMNS. */
    if (w->sent_in_set >= w->quota) {
        return w_cap(w, out, PROTO_CAP_QUOTA);
    }
    size_t g = 0;
    if (w_take(w, &g)) {
        w->have_chunk = true;
        w->chunk = g;
        w->chunk_rows = 0;
        return emit(out, ACT_STORE_ROW, PROTO_NONE, g, w->conn, w->req, 1, 0);
    }
    return w_act(w, out, ACT_WAIT, 0);
}

static bool w_columns(conn_worker *w, conn_actions *out) {
    if (w->state == W_CANCELLING || w->state == W_DRAINING) {
        w->dropped = true;
        return w_act(w, out, ACT_DISCARD, 0);
    }
    if (w->state != W_RUNNING) {
        return false;
    }
    if (!w_flush(w, out)) {
        return false;
    }
    size_t g = 0;
    if (!w_take(w, &g)) {
        return w_act(w, out, ACT_WAIT, 0); /* the UI always grants the spare */
    }
    w->in_set = true;
    w->sent_in_set = 1;
    w->sent_chunk = false;
    w->rows = 0;
    return w_send(w, out, PROTO_COLUMNS, g, w->req, 0, 0);
}

/* No more results: return what is unused and send the terminal reply. */
static bool w_end(conn_worker *w, conn_actions *out) {
    if (!w_busy(w)) {
        return false;
    }
    if (!w_flush(w, out) || !w_return_grants(w, out)) {
        return false;
    }
    uint64_t earlier = w->tags > 0 ? w->tags - 1 : 0;
    proto_kind k = PROTO_DONE;
    uint64_t aux = earlier;
    if (w->state == W_DRAINING) {
        k = PROTO_ERROR;
        aux = 0;
    } else if (w->state == W_CANCELLING && w->capped) {
        k = PROTO_CAPPED;
        aux = (uint64_t)w->cap;
    } else if (w->state == W_CANCELLING && w->dropped) {
        k = PROTO_CANCELLED;
        aux = 0;
    }
    w->state = W_READY;
    return w_send(w, out, k, w->terminal, w->req, w->rows, aux);
}

bool conn_worker_step(conn_worker *w, conn_worker_event e, conn_actions *out) {
    bool ok = false;
    switch (e.kind) {
    case W_EV_MSG:
        ok = w_msg(w, e, out);
        break;
    case W_EV_CONNECTED:
        ok = w->state == W_CONNECTING;
        if (ok) {
            w->state = W_READY;
            ok = w_reserved(w, out, PROTO_CONNECTED, 0);
        }
        break;
    case W_EV_CONN_FAILED:
        ok = w->state == W_CONNECTING && e.aux < (uint64_t)PROTO_FAIL_COUNT;
        if (ok) {
            w->state = W_CLOSING;
            ok = w_reserved(w, out, PROTO_CONN_FAILED, e.aux) && w_act(w, out, ACT_FINISH, 0);
        }
        break;
    case W_EV_COLUMNS:
        ok = w_columns(w, out);
        break;
    case W_EV_ROW:
        ok = w_row(w, out);
        break;
    case W_EV_ROW_STORED:
        ok = w->state == W_RUNNING && w->have_chunk;
        if (ok) {
            w->chunk_rows++;
            w->rows++;
            if (w->chunk_rows == 1) {
                w->chunk_first_ms = e.now_ms;
            }
            if (!w->sent_chunk && w->chunk_rows >= CONN_FIRST_CHUNK_ROWS) {
                ok = w_flush(w, out);
            }
        }
        break;
    case W_EV_CHUNK_FULL:
        ok = w->state == W_RUNNING && w->have_chunk;
        if (ok) {
            /* A row too big for an empty chunk can never be stored: cap, and
             * the redelivered ROW is discarded in Cancelling. */
            if (w->chunk_rows == 0) {
                w->dropped = true;
                ok = w_cancel(w, out, true, PROTO_CAP_MEMORY);
            } else {
                ok = w_flush(w, out);
            }
        }
        break;
    case W_EV_TAG:
        ok = w_busy(w);
        if (ok) {
            w->tags++;
            w->in_set = false;
        }
        break;
    case W_EV_ERROR:
        ok = w_busy(w);
        if (ok && w->state == W_RUNNING) {
            w->state = W_DRAINING;
            ok = w_flush(w, out);
        } else if (ok && w->state == W_CANCELLING) {
            w->dropped = true;
        }
        break;
    case W_EV_END:
        ok = w_end(w, out);
        break;
    case W_EV_LOST:
        ok = true;
        if (w->state == W_CONNECTING || w->state == W_READY || w_busy(w)) {
            ok = w_release(w, out) && w_act(w, out, ACT_FINISH, 0);
            w->state = W_CLOSING;
            ok = w_reserved(w, out, PROTO_LOST, 0) && ok;
        }
        break;
    default:
        ok = true;
        if (w->state == W_RUNNING && w->have_chunk && w->chunk_rows > 0 &&
            e.now_ms - w->chunk_first_ms >= CONN_FLUSH_MS) {
            ok = w_flush(w, out);
        }
        break;
    }
    return ok && !out->failed;
}
