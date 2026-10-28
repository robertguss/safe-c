#include "worker.h"
#include "sc_pq.h"

#define WORKER_TICK_MS 50u
/* Results pump handles before the loop looks at its requests again, so a
 * CLOSE or CANCEL is seen while a fast server keeps the socket full. */
#define PUMP_BATCH 256u

_Static_assert(PROTO_CELL_NULL == SC_PQ_CELL_NULL, "cell flags match the adapter");
_Static_assert(PROTO_CELL_TRUNCATED == SC_PQ_CELL_TRUNCATED, "cell flags match the adapter");

/* What the glue did with the last ROW event. */
typedef enum { ROW_NONE, ROW_FIT, ROW_FULL } row_outcome;

typedef struct {
    worker_ctx *ctx;
    conn_worker m;
    conn_actions acts;
    wire_side side;
    sc_pq pq;
    bool connecting;
    bool connect_fresh; /* started this pass: wait for the socket before the first poll */
    sc_pq_start start;  /* why the last connect start failed */
    bool more;          /* pump stopped at PUMP_BATCH with results left: do not sleep */
    bool want_write;
    bool flushing; /* the query is still being written */
    bool cancelling; /* libpq's non-blocking cancel is under way (KTD7) */
    bool cancel_failed; /* the cancel failed: tell the machine after the step */
    bool cancel_done;   /* the server has the cancel: tell the machine after the step */
    bool send_failed; /* libpq refused the query: report it as an error */
    /* The current libpq result and the next row of it to deliver. */
    bool have_res;
    sc_pq_next_kind res;
    size_t row;
    size_t rows;
    bool set_open; /* COLUMNS went out (or was discarded) for this statement */
    bool waiting;  /* the machine waits for credit: deliver the event again */
    row_outcome outcome;
    bool discarded;
    proto_writer chunk;
    bool chunk_open;
    size_t chunk_arena;
    proto_writer term; /* the terminal reply, filled as tags and errors arrive */
    bool term_open;
    bool exit;
    bool failed;
} worker;

static sc_arena *arena_at(worker *w, size_t i) {
    return sc_mem_arena(w->ctx->pool, i);
}

/* A byte string from a literal, as a string field. */
static bool put_lit(proto_writer *pw, const uint8_t *lit, size_t n) {
    if (!proto_put_u64(pw, (uint64_t)n)) {
        return false;
    }
    sc_buf b = proto_body(pw);
    return sc_buf_lit(pw->arena, &b, lit, n) && proto_set_body(pw, b);
}
#define PUT_LIT(pw, lit) put_lit((pw), (const uint8_t *)(lit), sizeof(lit) - 1)

/* Starts a message in arena `i`, emptied first. */
static bool begin(worker *w, proto_writer *pw, size_t i, proto_header h) {
    return sc_mem_reset(w->ctx->pool, i) && proto_begin(pw, arena_at(w, i), h);
}

static void ring_ui(worker *w) {
    if (sc_flag_set(w->ctx->pending)) {
        w->ctx->ring(w->ctx->ring_arg);
    }
}

static bool deliver(worker *w, size_t i) {
    if (!wire_mbox_send(&w->side, w->ctx->replies, i)) {
        return false;
    }
    ring_ui(w);
    return true;
}

static bool open_terminal(worker *w, size_t i) {
    proto_header h = {PROTO_DONE, w->ctx->conn, w->m.req, 0, 0, 0};
    w->term_open = begin(w, &w->term, i, h);
    return w->term_open;
}

static bool send_reply(worker *w, conn_action a) {
    proto_header h = {a.msg, a.conn, a.req, a.arg, a.aux, 0};
    proto_writer pw;
    if (a.msg == PROTO_CHUNK) {
        if (!w->chunk_open || w->chunk_arena != a.arena) {
            return false;
        }
        w->chunk_open = false;
        w->chunk.h = h;
        return proto_end(&w->chunk) && deliver(w, a.arena);
    }
    if (a.msg == PROTO_DONE || a.msg == PROTO_ERROR || a.msg == PROTO_CAPPED ||
        a.msg == PROTO_CANCELLED) {
        /* A run cancelled while held for a cancel never opened its reply. */
        if (!w->term_open && !open_terminal(w, a.arena)) {
            return false;
        }
        w->term_open = false;
        w->term.h = h;
        return proto_put_u64(&w->term, PROTO_ITEM_END) &&
               proto_put_u64(&w->term, sc_pq_transaction(w->pq)) && proto_end(&w->term) &&
               deliver(w, a.arena);
    }
    if (w->chunk_open && w->chunk_arena == a.arena) {
        w->chunk_open = false; /* an empty chunk goes back unused */
    }
    if (a.msg == PROTO_RETURN && a.arena == w->m.terminal) {
        w->term_open = false;
    }
    if (!begin(w, &pw, a.arena, h)) {
        return false;
    }
    bool ok = true;
    if (a.msg == PROTO_CONN_FAILED && !w->pq.live) {
        /* sc_pq_connect_start refused: libpq has no message, or one that
         * quotes the connection string and so may hold a password. */
        ok = w->start == SC_PQ_START_MALFORMED
                 ? PUT_LIT(&pw, "The connection string is malformed. (libpq's own message is not "
                                "shown: it repeats the string, password included.)")
             : w->start == SC_PQ_START_TOO_MANY
                 ? PUT_LIT(&pw, "Too many connections are open in this app.")
                 : PUT_LIT(&pw, "libpq could not start the connection.");
        return ok && proto_end(&pw) && deliver(w, a.arena);
    }
    if (a.msg == PROTO_COLUMNS || a.msg == PROTO_CONN_FAILED) {
        sc_buf b = proto_body(&pw);
        ok = (a.msg == PROTO_COLUMNS ? sc_pq_put_columns(w->pq, pw.arena, &b)
                                      : sc_pq_put_error_message(w->pq, pw.arena, &b)) &&
             proto_set_body(&pw, b);
    }
    return ok && proto_end(&pw) && deliver(w, a.arena);
}

/* The body's first string, which holds the conninfo or the SQL. */
static bool read_text(worker *w, size_t i, sc_str *out) {
    proto_header h;
    proto_reader r;
    return proto_read(arena_at(w, i), &h, &r) && proto_get_str(&r, out);
}

static void start_connect(worker *w, size_t i) {
    sc_str info, password;
    w->connecting = true;
    w->connect_fresh = true;
    w->want_write = true;
    w->start = SC_PQ_START_MALFORMED;
    if (proto_get_open(arena_at(w, i), &info, &password)) {
        sc_pq_role role = w->ctx->meta ? SC_PQ_META : SC_PQ_QUERY;
        w->start = sc_pq_start_reason(sc_pq_connect_start(&w->pq, arena_at(w, i), info, password, role));
        (void)sc_mem_erase(arena_at(w, i), password);
    }
}

static bool start_query(worker *w, size_t i) {
    sc_str sql;
    if (!read_text(w, i, &sql)) {
        return false;
    }
    sc_result_size sent = sc_pq_send_query(w->pq, arena_at(w, i), sql);
    w->have_res = false;
    w->set_open = false;
    w->waiting = false;
    /* libpq copied the SQL, so the arena now collects the terminal reply. */
    if (!open_terminal(w, i)) {
        return false;
    }
    w->flushing = sent.ok && sc_pq_flush_pending(w->pq);
    w->send_failed = !sent.ok;
    return true;
}

static bool store_row(worker *w, conn_action a) {
    sc_arena *ar = arena_at(w, a.arena);
    if (a.arg == 1) {
        proto_header h = {PROTO_CHUNK, w->ctx->conn, a.req, 0, 0, 0};
        if (!begin(w, &w->chunk, a.arena, h)) {
            return false;
        }
        w->chunk_open = true;
        w->chunk_arena = a.arena;
    }
    if (!w->chunk_open || w->chunk_arena != a.arena) {
        return false;
    }
    sc_buf b = proto_body(&w->chunk);
    bool fit = sc_pq_put_row(w->pq, w->row, PROTO_CELL_CAP, ar, &b);
    w->outcome = fit ? ROW_FIT : ROW_FULL;
    return proto_set_body(&w->chunk, b);
}

static void note_cancel(worker *w, sc_pq_cancel_status st) {
    w->cancelling = st == SC_PQ_CANCEL_PENDING;
    w->cancel_failed = st == SC_PQ_CANCEL_FAILED;
    w->cancel_done = st == SC_PQ_CANCEL_SENT;
}

static bool perform(worker *w) {
    bool ok = true;
    for (size_t i = 0; i < w->acts.count && ok; i++) {
        conn_action a = conn_action_at(&w->acts, i);
        switch (a.kind) {
        case ACT_SEND:
            ok = send_reply(w, a);
            break;
        case ACT_CONNECT:
            start_connect(w, a.arena);
            break;
        case ACT_QUERY:
            ok = start_query(w, a.arena);
            break;
        case ACT_STORE_ROW:
            ok = store_row(w, a);
            break;
        case ACT_WAIT:
            w->waiting = true;
            break;
        case ACT_DISCARD:
            w->discarded = true;
            break;
        case ACT_CANCEL:
            note_cancel(w, sc_pq_cancel_start(w->pq));
            break;
        case ACT_FINISH:
            sc_pq_finish(&w->pq);
            w->connecting = false;
            w->cancelling = false;
            w->flushing = false;
            w->have_res = false;
            break;
        case ACT_EXIT:
            w->exit = true;
            break;
        default:
            ok = false;
            break;
        }
    }
    conn_actions_clear(&w->acts);
    return ok;
}

static bool step_ev(worker *w, conn_worker_event e) {
    w->outcome = ROW_NONE;
    w->discarded = false;
    bool ok = conn_worker_step(&w->m, e, &w->acts);
    ok = perform(w) && ok;
    if (!ok) {
        w->failed = true;
    }
    return ok;
}

static bool step(worker *w, conn_worker_event_kind k, uint64_t aux) {
    conn_worker_event e = {k, 0, {PROTO_NONE, 0, 0, 0, 0, 0}, aux, sc_clock_ms()};
    return step_ev(w, e);
}

/* Reports a cancel that failed or went through, once the step that started
 * it is done: a run held for it goes out then. */
static void report_cancel(worker *w) {
    if (w->cancel_failed) {
        w->cancel_failed = false;
        (void)step(w, W_EV_CANCEL_FAILED, 0);
    }
    if (w->cancel_done) {
        w->cancel_done = false;
        (void)step(w, W_EV_CANCEL_DONE, 0);
    }
}

static bool busy(const worker *w) {
    return w->m.state == W_RUNNING || w->m.state == W_CANCELLING || w->m.state == W_DRAINING;
}

/* Room the terminal reply keeps for its PROTO_ITEM_END and status words. */
#define TERM_RESERVE 16u

/* Keeps an item just appended to the terminal reply, or takes it back when it
 * did not fit or left no room for the end. A dropped tag or error loses only
 * its text: the reply still goes out, and the machine counts the tags. */
static void keep_item(worker *w, proto_writer before, bool ok) {
    sc_buf b = proto_body(&w->term);
    if (!ok || b.overflow || b.dst.len - b.len < TERM_RESERVE) {
        w->term = before;
    }
}

static void tag(worker *w) {
    if (step(w, W_EV_TAG, 0) && w->term_open) {
        proto_writer before = w->term;
        bool ok = proto_put_u64(&w->term, PROTO_ITEM_TAG);
        sc_buf b = proto_body(&w->term);
        ok = ok && sc_pq_put_tag(w->pq, w->term.arena, &b) && proto_set_body(&w->term, b);
        keep_item(w, before, ok);
    }
    w->set_open = false;
}

typedef enum { ERR_RESULT, ERR_COPY, ERR_UNSENT } error_kind;

/* An error result, a COPY (which the explorer reports as an error), or a query
 * libpq would not send. Only the error that ends a running query is reported;
 * the one a cancel causes is not. */
static void error(worker *w, error_kind kind) {
    bool running = w->m.state == W_RUNNING;
    w->set_open = false;
    if (!step(w, W_EV_ERROR, 0) || !running || w->m.state != W_DRAINING || !w->term_open) {
        return;
    }
    proto_writer before = w->term;
    bool ok = proto_put_u64(&w->term, PROTO_ITEM_ERROR);
    if (kind == ERR_COPY) {
        ok = ok && PUT_LIT(&w->term, "0A000") && PUT_LIT(&w->term, "COPY is not supported") &&
             PUT_LIT(&w->term, "") && PUT_LIT(&w->term, "") && proto_put_u64(&w->term, 0);
    } else if (kind == ERR_UNSENT) {
        ok = ok && PUT_LIT(&w->term, "22021") &&
             PUT_LIT(&w->term, "the query could not be sent (it may hold a NUL byte)") &&
             PUT_LIT(&w->term, "") && PUT_LIT(&w->term, "") && proto_put_u64(&w->term, 0);
    } else {
        sc_buf b = proto_body(&w->term);
        ok = ok && sc_pq_put_error(w->pq, w->term.arena, &b) && proto_set_body(&w->term, b);
    }
    keep_item(w, before, ok);
}

/* Delivers the next row. False while the machine waits for credit. */
static bool feed_row(worker *w) {
    if (!step(w, W_EV_ROW, 0) || w->waiting) {
        return false;
    }
    if (w->outcome == ROW_FIT) {
        w->row++;
        return step(w, W_EV_ROW_STORED, 0);
    }
    if (w->outcome == ROW_FULL) {
        return step(w, W_EV_CHUNK_FULL, 0); /* the same row goes again */
    }
    if (!w->discarded) {
        w->failed = true; /* the machine neither stored nor discarded the row */
        return false;
    }
    w->row++;
    return true;
}

/* Handles the current result. False while it waits or after a failure. */
static bool handle(worker *w) {
    if (w->res == SC_PQ_ROWS || w->res == SC_PQ_TUPLES) {
        if (!w->set_open && (w->res == SC_PQ_ROWS || sc_pq_columns(w->pq) > 0)) {
            if (!step(w, W_EV_COLUMNS, 0) || w->waiting) {
                return false;
            }
            w->set_open = true;
        }
        while (w->row < w->rows && !w->failed) {
            if (!feed_row(w)) {
                return false;
            }
        }
        if (w->res == SC_PQ_TUPLES) {
            tag(w);
        }
    } else if (w->res == SC_PQ_COMMAND) {
        tag(w);
    } else if (w->res == SC_PQ_ERROR || w->res == SC_PQ_COPY) {
        error(w, w->res == SC_PQ_COPY ? ERR_COPY : ERR_RESULT);
    } else if (w->res == SC_PQ_END) {
        w->set_open = false;
        (void)step(w, W_EV_END, 0);
    }
    return !w->failed;
}

/* Feeds libpq results to the machine until it waits, the socket has no more,
 * or PUMP_BATCH results have gone by. */
static void pump(worker *w) {
    uint32_t handled = 0;
    w->more = false;
    if (w->send_failed && busy(w)) {
        w->send_failed = false;
        error(w, ERR_UNSENT); /* the next result is END, or LOST */
    }
    while (!w->failed && !w->waiting && busy(w) && !w->m.query_held && w->pq.live && !w->flushing) {
        if (!w->have_res) {
            sc_pq_next_kind k = sc_pq_next(w->pq);
            if (k == SC_PQ_BUSY) {
                return;
            }
            if (k == SC_PQ_LOST) {
                (void)step(w, W_EV_LOST, 0);
                return;
            }
            w->have_res = true;
            w->res = k;
            w->row = 0;
            w->rows = sc_pq_rows(w->pq);
        }
        if (!handle(w)) {
            return;
        }
        w->have_res = false;
        if (++handled >= PUMP_BATCH) {
            w->more = true;
            return;
        }
    }
    if (busy(w) && !w->pq.live) {
        (void)step(w, W_EV_LOST, 0);
    }
}

static void poll_connect(worker *w, bool ready) {
    if (!w->pq.live) {
        w->connecting = false;
        (void)step(w, W_EV_CONN_FAILED, PROTO_FAIL_NETWORK);
        return;
    }
    sc_pq_connect_status st = sc_pq_connect_poll(w->pq, ready, &w->want_write);
    if (st == SC_PQ_CONNECTING) {
        return;
    }
    w->connecting = false;
    if (st == SC_PQ_CONNECTED) {
        (void)step(w, W_EV_CONNECTED, 0);
    } else {
        (void)step(w, W_EV_CONN_FAILED,
                   st == SC_PQ_NEEDS_PASSWORD ? PROTO_FAIL_NEEDS_PASSWORD :
                   st == SC_PQ_FAILED_AUTH ? PROTO_FAIL_AUTH : PROTO_FAIL_NETWORK);
    }
}

static void drain_requests(worker *w) {
    size_t i = 0;
    sc_chan_status st = SC_CHAN_EMPTY;
    while (!w->failed && !w->exit &&
           (st = wire_chan_recv(&w->side, w->ctx->requests, &i)) == SC_CHAN_OK) {
        proto_header h;
        proto_reader r;
        sc_arena *ar = arena_at(w, i);
        if (ar == NULL || !proto_read(ar, &h, &r)) {
            w->failed = true;
            return;
        }
        conn_worker_event e = {W_EV_MSG, i, h, 0, sc_clock_ms()};
        (void)step_ev(w, e);
        report_cancel(w);
        /* A grant may be what a waiting event needed. */
        w->waiting = false;
    }
    if (st == SC_CHAN_CLOSED) {
        w->exit = true; /* the UI gave up on this worker without a CLOSE (link_join) */
    }
}

void worker_main(void *arg) {
    worker_ctx *ctx = arg;
    worker w = {0};
    w.ctx = ctx;
    w.failed = !conn_worker_init(&w.m, ctx->book, ctx->conn, ctx->max_grants) ||
               !conn_actions_init(&w.acts, ctx->book, ctx->max_actions) ||
               !wire_side_init(&w.side, ctx->handles, ctx->max_actions);
    while (!w.exit && !w.failed) {
        bool sock = false, cancel_ready = false;
        /* While the machine waits for credit, a readable socket would spin
         * the wait: only the wake can bring the grant. */
        if (w.pq.live && !w.waiting) {
            sc_result_size r =
                sc_pq_wait(w.pq, ctx->wake, w.want_write || w.flushing, w.more ? 0 : WORKER_TICK_MS);
            sock = r.ok && (r.value & 1u) != 0;
            cancel_ready = r.ok && (r.value & 4u) != 0;
        } else {
            (void)sc_wake_wait(ctx->wake, WORKER_TICK_MS);
        }
        (void)sc_wake_drain(ctx->wake);
        if (w.cancelling && w.pq.live) {
            note_cancel(&w, sc_pq_cancel_poll(w.pq, cancel_ready));
        }
        drain_requests(&w);
        if (w.connecting && !w.exit) {
            /* libpq: a connect started this pass waits for the socket first. */
            poll_connect(&w, sock && !w.connect_fresh);
            w.connect_fresh = false;
        } else if (sock && !w.exit && w.pq.live && !busy(&w) && !sc_pq_idle_alive(w.pq)) {
            (void)step(&w, W_EV_LOST, 0); /* an idle connection dropped */
        }
        if (w.flushing) {
            w.flushing = sc_pq_flush_pending(w.pq);
        }
        report_cancel(&w);
        pump(&w);
        report_cancel(&w);
        if (!w.exit && w.m.state == W_RUNNING) {
            (void)step(&w, W_EV_TICK, 0);
        }
    }
    if (w.pq.live) {
        sc_pq_finish(&w.pq);
    }
    ctx->broken = w.failed;
    /* Tell the UI the thread is returning, BYE or not, so it never waits on a
     * worker that is gone. */
    (void)sc_flag_set(&ctx->done);
    ctx->ring(ctx->ring_arg);
}
