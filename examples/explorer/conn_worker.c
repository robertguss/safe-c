#include "conn_worker.h"

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
    return conn_emit(out, ACT_SEND, k, arena, w->conn, req, arg, aux);
}

static bool w_act(conn_worker *w, conn_actions *out, act_kind k, size_t arena) {
    return conn_emit(out, k, PROTO_NONE, arena, w->conn, w->req, 0, 0);
}

/* Sends LOST, CONNECTED, CONN_FAILED, CANCEL_FAILED or BYE in the reserved
 * arena, or marks it pending until the UI hands the arena back. */
static bool w_reserved(conn_worker *w, conn_actions *out, proto_kind k, uint64_t aux) {
    if (!w->have_reserved) {
        if (k == PROTO_LOST) {
            w->lost_pending = true;
        } else if (k == PROTO_CANCEL_FAILED) {
            w->cancel_failed_pending = true;
        } else if (k == PROTO_BYE) {
            w->bye_pending = true;
        } else {
            return false;
        }
        return true;
    }
    w->have_reserved = false;
    if (!w_send(w, out, k, w->reserved, k == PROTO_CANCEL_FAILED ? w->req : 0, 0, aux)) {
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
    w->cancel_out = true;
    return w_flush(w, out) && w_act(w, out, ACT_CANCEL, 0);
}

/* libpq's cancel is through or has failed: a run held for it goes out. */
static bool w_cancel_over(conn_worker *w, conn_actions *out) {
    w->cancel_out = false;
    if (!w->query_held) {
        return true;
    }
    w->query_held = false;
    return w_act(w, out, ACT_QUERY, w->terminal);
}

/* The connection is closed: no cancel is under way, and a held run never goes out. */
static void w_forget_cancel(conn_worker *w) {
    w->cancel_out = false;
    w->query_held = false;
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

static bool w_end(conn_worker *w, conn_actions *out);

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
        if (w->cancel_failed_pending) {
            w->cancel_failed_pending = false;
            return w_reserved(w, out, PROTO_CANCEL_FAILED, 0);
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
        /* A cancel still under way could land on this query: it waits. */
        w->query_held = w->cancel_out;
        return w->query_held || w_act(w, out, ACT_QUERY, e.arena);
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
        if (e.h.req == w->req && w->state == W_RUNNING && w->query_held) {
            /* Its query never went out: it ends here, with no rows. */
            proto_cap why = (proto_cap)e.h.aux;
            w->query_held = false;
            w->capped = why != PROTO_CAP_USER;
            w->cap = why;
            w->dropped = true;
            w->state = W_CANCELLING;
            ok = w_end(w, out);
        } else if (e.h.req == w->req && w->state == W_RUNNING) {
            proto_cap why = (proto_cap)e.h.aux;
            ok = w_cancel(w, out, why != PROTO_CAP_USER, why);
        }
        return w_send(w, out, PROTO_RETURN, e.arena, e.h.req, 0, 0) && ok;
    }
    if (k == PROTO_CLOSE) {
        bool ok = w_release(w, out);
        w_forget_cancel(w);
        if (w->state != W_IDLE && w->state != W_CLOSING && w->state != W_CLOSED) {
            ok = w_act(w, out, ACT_FINISH, 0) && ok;
        }
        ok = w_send(w, out, PROTO_RETURN, e.arena, e.h.req, 0, 0) && ok;
        w->state = W_CLOSED;
        w->lost_pending = false;
        w->cancel_failed_pending = false;
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
        return conn_emit(out, ACT_STORE_ROW, PROTO_NONE, w->chunk, w->conn, w->req, 0, 0);
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
        return conn_emit(out, ACT_STORE_ROW, PROTO_NONE, g, w->conn, w->req, 1, 0);
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

/* A libpq result event, which no held run can have. */
static bool w_result_event(conn_worker_event_kind k) {
    return k == W_EV_COLUMNS || k == W_EV_ROW || k == W_EV_ROW_STORED || k == W_EV_CHUNK_FULL ||
           k == W_EV_TAG || k == W_EV_ERROR || k == W_EV_END;
}

bool conn_worker_step(conn_worker *w, conn_worker_event e, conn_actions *out) {
    bool ok = false;
    if (w->query_held && w_result_event(e.kind)) {
        return false;
    }
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
            w_forget_cancel(w);
            w->state = W_CLOSING;
            ok = w_reserved(w, out, PROTO_LOST, 0) && ok;
        }
        break;
    case W_EV_CANCEL_FAILED:
        /* Only the user's cancel is reported: a cap's rows drain anyway. */
        ok = w->state != W_CANCELLING || w->capped ||
             w_reserved(w, out, PROTO_CANCEL_FAILED, 0);
        ok = w_cancel_over(w, out) && ok;
        break;
    case W_EV_CANCEL_DONE:
        ok = w_cancel_over(w, out);
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
