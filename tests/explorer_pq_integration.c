/* Integration test for the explorer's worker against a real Postgres server
 * (plan U7): adapters/pq.c, examples/explorer/worker.c and the UI side in
 * link.c, with real threads. Run it through `make -C examples/explorer pq-test`,
 * which starts a temporary server with pgtest.sh and exports PG*.
 *
 * The name does not end in _test.c, so `make test` (no libpq) skips it. */
#include "../examples/explorer/link.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sc_wake uiw;
static conn_link L, L2;
static alignas(8) uint8_t scratch_mem[1u << 16];
static sc_arena scratch;

static void ring(void *arg) {
    (void)sc_wake_ring(*(sc_wake *)arg);
}

static sc_str str(const char *s) {
    assert(sc_arena_reset(&scratch));
    return s[0] == 0 ? (sc_str){NULL, 0, scratch.epoch}
                     : sc_str_from_lit(&scratch, (const uint8_t *)s, strlen(s));
}

static bool active(const conn_link *l) {
    query_state q = l->ui.q;
    return q == Q_QUEUED || q == Q_RUNNING || q == Q_STREAMING || q == Q_CANCELLING;
}

/* Polls `l` until `cond` holds, failing after `ms`. */
#define WAIT(l, cond, ms)                                                                          \
    do {                                                                                           \
        uint64_t end_ = sc_clock_ms() + (ms);                                                      \
        while (!(cond)) {                                                                          \
            if (sc_clock_ms() > end_) {                                                            \
                fprintf(stderr, "timed out at line %d: %s\n", __LINE__, #cond);                    \
                abort();                                                                           \
            }                                                                                      \
            (void)sc_wake_wait(uiw, 50);                                                           \
            (void)sc_wake_drain(uiw);                                                              \
            (void)link_poll(l);                                                                    \
            assert(!(l)->broken);                                                                  \
        }                                                                                          \
    } while (0)

static const conn_limits roomy = {4, 64, 100000};

static void connect_as(conn_link *l, uint32_t id, conn_limits lim, const char *info) {
    assert(link_init(l, id, 96, lim, ring, &uiw));
    assert(link_open(l, &scratch, str(info)));
    WAIT(l, l->ui.state != UI_CONN_CONNECTING, 10000);
}

static void close_link(conn_link *l) {
    uint64_t t0 = sc_clock_ms();
    assert(link_close(l));
    WAIT(l, l->ui.state == UI_CONN_CLOSED, 2000);
    assert(link_join(l));
    assert(sc_clock_ms() - t0 < 2000);
    link_free(l);
}

static void run(conn_link *l, const char *sql) {
    assert(link_run(l, &scratch, str(sql)));
    assert(!l->refused);
    WAIT(l, !active(l), 20000);
}

/* ---- Reading what the UI holds ---- */

typedef struct {
    uint64_t tags;
    char last_tag[64];
    bool error;
    char sqlstate[8];
    char message[256];
    uint64_t position;
    uint64_t txn;
} term_info;

static void copy_str(char *dst, size_t cap, sc_str s) {
    size_t n = s.len < cap - 1 ? s.len : cap - 1;
    if (n > 0)
        memcpy(dst, s.ptr, n);
    dst[n] = 0;
}

static term_info read_term(conn_link *l) {
    term_info t;
    memset(&t, 0, sizeof t);
    assert(l->ui.terminal_kept);
    proto_header h;
    proto_reader r;
    assert(proto_read(link_arena(l, l->ui.terminal), &h, &r));
    for (;;) {
        uint64_t kind = 0;
        sc_str s;
        assert(proto_get_u64(&r, &kind));
        if (kind == PROTO_ITEM_END) {
            assert(proto_get_u64(&r, &t.txn));
            break;
        }
        if (kind == PROTO_ITEM_TAG) {
            assert(proto_get_str(&r, &s));
            copy_str(t.last_tag, sizeof t.last_tag, s);
            t.tags++;
            continue;
        }
        assert(kind == PROTO_ITEM_ERROR);
        t.error = true;
        assert(proto_get_str(&r, &s));
        copy_str(t.sqlstate, sizeof t.sqlstate, s);
        assert(proto_get_str(&r, &s));
        copy_str(t.message, sizeof t.message, s);
        assert(proto_get_str(&r, &s) && proto_get_str(&r, &s));
        assert(proto_get_u64(&r, &t.position));
    }
    return t;
}

typedef void (*cell_fn)(void *ctx, uint64_t row, uint64_t col, proto_cell c);

/* Walks every cell of the current set in order; returns the rows seen. */
static uint64_t walk(conn_link *l, cell_fn fn, void *ctx) {
    if (l->ui.held_count == 0)
        return 0;
    proto_header h;
    proto_reader r;
    assert(proto_read(link_arena(l, conn_ui_held(&l->ui, 0)), &h, &r));
    assert(h.kind == PROTO_COLUMNS);
    uint64_t cols = 0;
    assert(proto_get_u64(&r, &cols));
    uint64_t row = 0;
    for (size_t i = 1; i < l->ui.held_count; i++) {
        assert(proto_read(link_arena(l, conn_ui_held(&l->ui, i)), &h, &r));
        assert(h.kind == PROTO_CHUNK && h.req == l->ui.req);
        for (uint64_t k = 0; k < h.arg; k++, row++) {
            for (uint64_t c = 0; c < cols; c++) {
                proto_cell cell;
                assert(proto_get_cell(&r, &cell));
                if (fn != NULL)
                    fn(ctx, row, c, cell);
            }
        }
        uint64_t extra;
        assert(!proto_get_u64(&r, &extra)); /* nothing after the rows */
    }
    return row;
}

static uint64_t num(proto_cell c) {
    char buf[32];
    copy_str(buf, sizeof buf, c.bytes);
    return strtoull(buf, NULL, 10);
}

static void check_series(void *ctx, uint64_t row, uint64_t col, proto_cell c) {
    (void)ctx;
    assert(!c.null && !c.truncated && num(c) == row + 1);
    (void)col;
}

/* ---- Scenarios ---- */

static void stream_25000(void) {
    connect_as(&L, 1, roomy, "");
    assert(L.ui.state == UI_CONN_CONNECTED);
    run(&L, "SELECT g, g::text FROM generate_series(1, 25000) g");
    assert(L.ui.q == Q_ROWS && L.ui.rows == 25000 && L.ui.held_count > 2);
    assert(walk(&L, check_series, NULL) == 25000);
    term_info t = read_term(&L);
    assert(t.tags == 1 && strcmp(t.last_tag, "SELECT 25000") == 0 && t.txn == 0);

    run(&L, "SELECT 1 WHERE false");
    assert(L.ui.q == Q_ROWS && L.ui.rows == 0 && L.ui.held_count == 1);
    close_link(&L);
    puts("ok stream 25,000 rows; empty set");
}

static void commands_and_copy(void) {
    connect_as(&L, 1, roomy, "");
    run(&L, "CREATE TEMP TABLE t(a int)");
    assert(L.ui.q == Q_COMMAND_TAG && strcmp(read_term(&L).last_tag, "CREATE TABLE") == 0);
    run(&L, "INSERT INTO t VALUES (1),(2),(3)");
    assert(L.ui.q == Q_COMMAND_TAG && strcmp(read_term(&L).last_tag, "INSERT 0 3") == 0);
    assert(L.ui.held_count == 0);

    const char *copies[] = {"COPY (SELECT 1) TO STDOUT", "COPY t FROM STDIN"};
    for (int i = 0; i < 2; i++) {
        run(&L, copies[i]);
        term_info t = read_term(&L);
        assert(L.ui.q == Q_ERROR && t.error);
        assert(strcmp(t.message, "COPY is not supported") == 0);
        run(&L, "SELECT 1");
        assert(L.ui.q == Q_ROWS && L.ui.rows == 1);
    }
    close_link(&L);
    puts("ok command tags; COPY refused both ways");
}

/* The plan's `THEN 1/0` fails at plan time: Postgres folds the constant 1/0
 * before any row is produced, so the division is written against g. */
static void error_after_rows(void) {
    connect_as(&L, 1, roomy, "");
    run(&L, "SELECT CASE WHEN g = 1001 THEN 1 / (g - 1001) ELSE g END FROM generate_series(1, 2000) g");
    term_info t = read_term(&L);
    assert(L.ui.q == Q_ERROR_AFTER_ROWS && L.ui.rows >= 1000);
    assert(t.error && strcmp(t.sqlstate, "22012") == 0);
    assert(walk(&L, check_series, NULL) == L.ui.rows);

    run(&L, "SELECT nope");
    t = read_term(&L);
    assert(L.ui.q == Q_ERROR && strcmp(t.sqlstate, "42703") == 0 && t.position == 8);
    close_link(&L);
    puts("ok error after rows; error with position");
}

static void cells(void *ctx, uint64_t row, uint64_t col, proto_cell c) {
    proto_cell *out = ctx;
    assert(row == 0);
    out[col] = c;
}

static void null_empty_truncated(void) {
    connect_as(&L, 1, roomy, "");
    proto_cell got[3];
    run(&L, "SELECT NULL::text, ''::text, repeat('x', 10240)");
    assert(walk(&L, cells, got) == 1);
    assert(got[0].null && got[0].bytes.len == 0);
    assert(!got[1].null && got[1].len == 0 && got[1].bytes.len == 0);
    assert(!got[2].null && got[2].truncated && got[2].len == 10240);
    assert(got[2].bytes.len == PROTO_CELL_CAP);
    close_link(&L);
    puts("ok NULL vs empty; 10 KiB cell truncated to 4 KiB");
}

static void cancel(void) {
    connect_as(&L, 1, roomy, "");
    assert(link_run(&L, &scratch, str("SELECT pg_sleep(30)")));
    uint64_t t0 = sc_clock_ms();
    while (sc_clock_ms() - t0 < 200) {
        (void)sc_wake_wait(uiw, 50);
        (void)link_poll(&L);
    }
    uint64_t t1 = sc_clock_ms();
    assert(link_cancel(&L));
    WAIT(&L, !active(&L), 2000);
    assert(L.ui.q == Q_CANCELLED && sc_clock_ms() - t1 < 2000);
    run(&L, "SELECT 1");
    assert(L.ui.q == Q_ROWS && L.ui.rows == 1);
    close_link(&L);
    puts("ok cancel within 2 s, then SELECT 1");
}

static void multi_statement(void) {
    connect_as(&L, 1, roomy, "");
    run(&L, "BEGIN; SELECT 1; SELECT 2");
    term_info t = read_term(&L);
    assert(L.ui.q == Q_ROWS && L.ui.rows == 1 && L.ui.earlier_tags == 2 && t.tags == 3);
    assert(t.txn == 2); /* inside the transaction */
    proto_cell got[1];
    assert(walk(&L, cells, got) == 1 && num(got[0]) == 2);
    run(&L, "ROLLBACK");
    close_link(&L);

    conn_limits limited = {4, 64, 10000};
    connect_as(&L, 1, limited, "");
    uint64_t t0 = sc_clock_ms();
    run(&L, "SELECT * FROM generate_series(1, 20000); SELECT 2 FROM pg_sleep(5)");
    assert(L.ui.q == Q_CAPPED && L.ui.cap == PROTO_CAP_LIMIT && L.ui.rows == 10000);
    assert(sc_clock_ms() - t0 < 4000); /* the later statement was cancelled */
    assert(walk(&L, check_series, NULL) == 10000);
    close_link(&L);
    puts("ok multi-statement keeps the last set; row limit caps");
}

static void connect_failures(void) {
    connect_as(&L, 1, roomy, "password=wrong");
    assert(L.ui.state == UI_CONN_AUTH_FAILED && L.failure.len > 0);
    close_link(&L);
    connect_as(&L, 1, roomy, "host=127.0.0.1 port=1 connect_timeout=2");
    assert(L.ui.state == UI_CONN_UNREACHABLE && L.failure.len > 0);
    close_link(&L);
    puts("ok wrong password is auth; closed port is network");
}

#define SLOW "SELECT g, pg_sleep(0.0005) FROM generate_series(1, 1000000) g"

static void terminate_and_close(void) {
    connect_as(&L, 1, roomy, "");
    assert(link_run(&L, &scratch, str(SLOW)));
    WAIT(&L, L.ui.rows > 0, 5000);
    connect_as(&L2, 2, roomy, "");
    run(&L2, "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
             "WHERE application_name = 'explorer' AND pid <> pg_backend_pid()");
    assert(L2.ui.q == Q_ROWS && L2.ui.rows == 1);
    close_link(&L2);
    WAIT(&L, L.ui.state == UI_CONN_DISCONNECTED, 5000);
    assert(L.ui.q == Q_DISCONNECTED && L.ui.rows > 0);
    close_link(&L);

    connect_as(&L, 1, roomy, "");
    assert(link_run(&L, &scratch, str(SLOW)));
    WAIT(&L, L.ui.rows > 0, 5000);
    close_link(&L); /* BYE and the join within 2 s */
    puts("ok terminated backend is LOST with rows kept; CLOSE while streaming");
}

int main(void) {
    sc_arena_init(&scratch, scratch_mem, sizeof scratch_mem);
    assert(sc_wake_new(&uiw).ok);
    stream_25000();
    commands_and_copy();
    error_after_rows();
    null_empty_truncated();
    cancel();
    multi_statement();
    connect_failures();
    terminate_and_close();
    sc_wake_free(&uiw);
    puts("explorer_pq_integration: ok");
    return 0;
}
