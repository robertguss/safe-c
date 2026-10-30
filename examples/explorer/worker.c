#include "worker.h"
#include "sc_db.h"
#include "result.h"

#define WORKER_TICK_MS 50u
/* Results pump handles before the loop looks at its requests again, so a
 * CLOSE or CANCEL is seen while a fast server keeps the socket full. */
#define PUMP_BATCH 256u

_Static_assert(PROTO_CELL_NULL == SC_DB_CELL_NULL, "cell flags match the adapter");
_Static_assert(PROTO_CELL_TRUNCATED == SC_DB_CELL_TRUNCATED, "cell flags match the adapter");
_Static_assert(SC_DB_CANCEL_DEADLINE_MS <= CONN_CANCEL_DEADLINE_MS, "the UI gives up on a cancel no sooner than the adapter");

/* What the glue did with the last ROW event. */
typedef enum { ROW_NONE, ROW_FIT, ROW_FULL, ROW_FAILED } row_outcome;

typedef struct {
    worker_ctx *ctx;
    conn_worker m;
    conn_actions acts;
    wire_side side;
    sc_db db;
    bool connecting;
    bool connect_fresh; /* started this pass: wait for the socket before the first poll */
    sc_db_start start;  /* why the last connect start failed */
    bool more;          /* pump stopped at PUMP_BATCH with results left: do not sleep */
    bool want_write;
    bool flushing; /* the query is still being written */
    bool cancelling; /* libpq's non-blocking cancel is under way (KTD7) */
    bool cancel_failed; /* the cancel failed: tell the machine after the step */
    bool cancel_done;   /* the server has the cancel: tell the machine after the step */
    bool send_failed; /* libpq refused the query: report it as an error */
    /* The current libpq result and the next row of it to deliver. */
    bool have_res;
    sc_db_next_kind res;
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
               proto_put_u64(&w->term, sc_db_transaction(w->db)) && proto_end(&w->term) &&
               deliver(w, a.arena);
    }
    if (w->chunk_open && w->chunk_arena == a.arena) {
        w->chunk_open = false; /* an empty chunk goes back unused */
    }
    if (a.msg == PROTO_RETURN && a.arena == w->m.terminal) {
        w->term_open = false;
    }
    if (a.msg == PROTO_COLUMNS && sc_db_label_columns(w->db)) h.aux = 1;
    if (!begin(w, &pw, a.arena, h)) {
        return false;
    }
    bool ok = true;
    if (a.msg == PROTO_CONN_FAILED && !w->db.live) {
        sc_buf b = proto_body(&pw);
        return sc_db_put_start_failed(w->db, w->start, pw.arena, &b) && proto_set_body(&pw, b) &&
               proto_end(&pw) && deliver(w, a.arena);
    }
    if (a.msg == PROTO_COLUMNS || a.msg == PROTO_CONN_FAILED) {
        sc_buf b = proto_body(&pw);
        ok = (a.msg == PROTO_COLUMNS ? sc_db_put_columns(w->db, pw.arena, &b)
                                      : sc_db_put_error_message(w->db, pw.arena, &b)) &&
             proto_set_body(&pw, b);
    }
    if (a.msg == PROTO_CONNECTED) {
        sc_buf b = proto_body(&pw);
        ok = sc_db_put_connected(w->db, pw.arena, &b) && proto_set_body(&pw, b);
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
    proto_header h;
    proto_reader r;
    w->connecting = true;
    w->connect_fresh = true;
    w->want_write = true;
    w->start = SC_DB_START_MALFORMED;
    if (proto_read(arena_at(w, i), &h, &r) && h.aux <= SC_DB_SQLITE &&
        proto_get_open(arena_at(w, i), &info, &password)) {
        sc_db_role role = w->ctx->meta ? SC_DB_META : SC_DB_QUERY;
        w->start = sc_db_start_reason(sc_db_connect_start(&w->db, (sc_db_engine)h.aux,
                                                         arena_at(w, i), info, password, role));
        sc_db_set_control(w->db, w->ctx->control);
        (void)sc_mem_erase(arena_at(w, i), password);
    }
}

static bool start_query(worker *w, size_t i) {
    sc_str sql;
    if (!read_text(w, i, &sql)) {
        return false;
    }
    sc_result_size sent = sc_db_send_query(w->db, arena_at(w, i), sql);
    w->have_res = false;
    w->set_open = false;
    w->waiting = false;
    /* libpq copied the SQL, so the arena now collects the terminal reply. */
    if (!open_terminal(w, i)) {
        return false;
    }
    w->flushing = sent.ok && sc_db_flush_pending(w->db);
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
    bool fit = sc_db_put_row(w->db, w->row, PROTO_CELL_CAP, ar, &b);
    w->outcome = fit ? ROW_FIT : sc_db_row_failed(w->db) ? ROW_FAILED : ROW_FULL;
    return proto_set_body(&w->chunk, b);
}

static void note_cancel(worker *w, sc_db_cancel_status st) {
    w->cancelling = st == SC_DB_CANCEL_PENDING;
    w->cancel_failed = st == SC_DB_CANCEL_FAILED;
    w->cancel_done = st == SC_DB_CANCEL_SENT;
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
            note_cancel(w, sc_db_cancel_start(w->db));
            break;
        case ACT_FINISH:
            sc_db_finish(&w->db);
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
        ok = ok && sc_db_put_tag(w->db, w->term.arena, &b) && proto_set_body(&w->term, b);
        keep_item(w, before, ok);
    }
    w->set_open = false;
}

typedef enum { ERR_RESULT, ERR_COPY, ERR_UNSENT, ERR_COLUMNS } error_kind;

/* An error result, a COPY (which the explorer reports as an error), or a query
 * the adapter would not send. Only the error that ends a running query is reported;
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
        sc_buf code = proto_body(&w->term);
        ok = ok && sc_db_put_unsent_code(w->db, w->term.arena, &code) && proto_set_body(&w->term, code) &&
             PUT_LIT(&w->term, "the query could not be sent (it may hold a NUL byte)") &&
             PUT_LIT(&w->term, "") && PUT_LIT(&w->term, "") && proto_put_u64(&w->term, 0);
    } else if (kind == ERR_COLUMNS) {
        ok = ok && PUT_LIT(&w->term, "Explorer") &&
             PUT_LIT(&w->term, "The result exceeds Explorer's 1,664-column limit.") &&
             PUT_LIT(&w->term, "") && PUT_LIT(&w->term, "") && proto_put_u64(&w->term, 0);
    } else {
        sc_buf b = proto_body(&w->term);
        ok = ok && sc_db_put_error(w->db, w->term.arena, &b) && proto_set_body(&w->term, b);
    }
    keep_item(w, before, ok);
}

/* Delivers the next row. False while the machine waits for credit. */
static void drain_requests(worker *w);

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
    if (w->outcome == ROW_FAILED) {
        drain_requests(w);
        if (w->exit || w->failed || !w->db.live) return false;
        error(w, ERR_RESULT);
        note_cancel(w, sc_db_cancel_start(w->db));
        w->row = w->rows;
        return true;
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
    if (w->res == SC_DB_ROWS || w->res == SC_DB_TUPLES) {
        if (w->m.state == W_RUNNING && sc_db_columns(w->db) > RESULT_MAX_COLUMNS) {
            error(w, ERR_COLUMNS);
            note_cancel(w, sc_db_cancel_start(w->db));
        }
        if (!w->set_open && (w->res == SC_DB_ROWS || sc_db_columns(w->db) > 0)) {
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
        if (w->res == SC_DB_TUPLES) {
            tag(w);
        }
    } else if (w->res == SC_DB_COMMAND) {
        tag(w);
    } else if (w->res == SC_DB_ERROR || w->res == SC_DB_COPY) {
        error(w, w->res == SC_DB_COPY ? ERR_COPY : ERR_RESULT);
    } else if (w->res == SC_DB_END) {
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
    while (!w->failed && !w->waiting && busy(w) && !w->m.query_held && w->db.live && !w->flushing) {
        if (!w->have_res) {
            sc_db_next_kind k = sc_db_next(w->db);
            if (sc_db_next_blocks(w->db)) {
                drain_requests(w);
                report_cancel(w);
                if (w->exit || w->failed || !w->db.live) return;
            }
            if (k == SC_DB_BUSY) {
                return;
            }
            if (k == SC_DB_LOST) {
                (void)step(w, W_EV_LOST, 0);
                return;
            }
            w->have_res = true;
            w->res = k;
            w->row = 0;
            w->rows = sc_db_rows(w->db);
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
    if (busy(w) && !w->db.live) {
        (void)step(w, W_EV_LOST, 0);
    }
}

static uint64_t fail_aux(sc_db_fail_kind kind) {
    switch (kind) {
    case SC_DB_FAIL_AUTH: return PROTO_FAIL_AUTH;
    case SC_DB_FAIL_PASSWORD: return PROTO_FAIL_NEEDS_PASSWORD;
    case SC_DB_FAIL_LOCAL: return PROTO_FAIL_LOCAL;
    case SC_DB_FAIL_NETWORK:
    default: return PROTO_FAIL_NETWORK;
    }
}

static void poll_connect(worker *w, bool ready) {
    if (!w->db.live) {
        w->connecting = false;
        (void)step(w, W_EV_CONN_FAILED, fail_aux(sc_db_fail_from_status(w->db, SC_DB_FAILED_NETWORK)));
        return;
    }
    sc_db_connect_status st = sc_db_connect_poll(w->db, ready, &w->want_write);
    if (st == SC_DB_CONNECTING) {
        return;
    }
    w->connecting = false;
    if (st == SC_DB_CONNECTED) {
        (void)step(w, W_EV_CONNECTED, 0);
    } else {
        (void)step(w, W_EV_CONN_FAILED, fail_aux(sc_db_fail_from_status(w->db, st)));
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
        if (w.db.live && !w.waiting) {
            sc_result_size r =
                sc_db_wait(w.db, ctx->wake, w.want_write || w.flushing, w.more ? 0 : WORKER_TICK_MS);
            sock = r.ok && (r.value & SC_DB_WAIT_SOCK) != 0;
            cancel_ready = r.ok && (r.value & SC_DB_WAIT_CANCEL) != 0;
        } else {
            (void)sc_wake_wait(ctx->wake, WORKER_TICK_MS);
        }
        (void)sc_wake_drain(ctx->wake);
        if (w.cancelling && w.db.live) {
            note_cancel(&w, sc_db_cancel_poll(w.db, cancel_ready));
        }
        drain_requests(&w);
        if (w.connecting && !w.exit) {
            /* libpq: a connect started this pass waits for the socket first. */
            poll_connect(&w, sock && !w.connect_fresh);
            w.connect_fresh = false;
        } else if (sock && !w.exit && w.db.live && !busy(&w) && !sc_db_idle_alive(w.db)) {
            (void)step(&w, W_EV_LOST, 0); /* an idle connection dropped */
        }
        if (w.flushing) {
            w.flushing = sc_db_flush_pending(w.db);
        }
        report_cancel(&w);
        pump(&w);
        report_cancel(&w);
        if (!w.exit && w.m.state == W_RUNNING) {
            (void)step(&w, W_EV_TICK, 0);
        }
    }
    if (w.db.live) {
        sc_db_finish(&w.db);
    }
    ctx->broken = w.failed;
    /* Tell the UI the thread is returning, BYE or not, so it never waits on a
     * worker that is gone. */
    (void)sc_flag_set(&ctx->done);
    ctx->ring(ctx->ring_arg);
}
