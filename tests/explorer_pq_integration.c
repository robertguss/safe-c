/* Integration test for the explorer's worker against a real Postgres server
 * (plan U7): adapters/pq.c, examples/explorer/worker.c and the UI side in
 * link.c, with real threads. Run it through `make -C examples/explorer pq-test`,
 * which starts a temporary server with pgtest.sh and exports PG*.
 *
 * The name does not end in _test.c, so `make test` (no libpq) skips it. */
#define _POSIX_C_SOURCE 200809L /* sockets and poll for the relay */
#include "../examples/explorer/meta.h"
#include "sc_pq.h"
#include <arpa/inet.h>
#include <assert.h>
#include <libpq-fe.h> /* LIBPQ_HAS_ASYNC_CANCEL only */
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static sc_wake uiw;
static link_pool P; /* every link's results, as main.c has it (KTD19) */
static conn_link L, L2, R, R2; /* R and R2: retiring links (U13) */
static alignas(8) uint8_t scratch_mem[1u << 18];
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

static void connect_on(conn_link *l, uint32_t id, conn_limits lim, const char *info,
                       sc_wake *bell) {
    assert(link_init(l, &P, BUDGET_QUERY, id, lim, ring, bell));
    assert(link_open(l, &scratch, str(info)));
    WAIT(l, l->ui.state != UI_CONN_CONNECTING, 10000);
}

static void connect_as(conn_link *l, uint32_t id, conn_limits lim, const char *info) {
    connect_on(l, id, lim, info, &uiw);
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
    size_t message_len;
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
        t.message_len = s.len;
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

static void cells(void *ctx, uint64_t row, uint64_t col, proto_cell c) {
    proto_cell *out = ctx;
    assert(row == 0);
    out[col] = c;
}


static void stream_25000(void) {
    connect_as(&L, 1, roomy, "");
    assert(L.ui.state == UI_CONN_CONNECTED);
    run(&L, "SELECT g, g::text FROM generate_series(1, 25000) g");
    assert(L.ui.q == Q_ROWS && L.ui.rows == 25000 && L.ui.held_count > 2);
    assert(walk(&L, check_series, NULL) == 25000);
    term_info t = read_term(&L);
    assert(t.tags == 1 && strcmp(t.last_tag, "SELECT 25000") == 0 && t.txn == 0);
    proto_header h;
    proto_reader r;
    assert(proto_read(link_arena(&L, conn_ui_held(&L.ui, 0)), &h, &r));
    uint64_t cols = 0, oid = 0;
    sc_str name;
    assert(proto_get_u64(&r, &cols) && cols == 2);
    assert(proto_get_str(&r, &name) && name.len == 1 && name.ptr[0] == 'g');
    assert(proto_get_u64(&r, &oid) && oid == 23); /* int4 */
    assert(proto_get_str(&r, &name) && name.len == 1 && name.ptr[0] == 'g');
    assert(proto_get_u64(&r, &oid) && oid == 25); /* text */

    proto_cell enc[1];
    run(&L, "SHOW client_encoding");
    assert(walk(&L, cells, enc) == 1 && enc[0].bytes.len == 4 && memcmp(enc[0].bytes.ptr, "UTF8", 4) == 0);

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

/* SQL with a NUL byte cannot be sent: an error, and the link stays usable. A
 * run while one is active is refused and keeps the running SQL. Rows that
 * stall before the first 100 still show within the 250 ms flush (KTD11). */
static void unsent_busy_and_flush(void) {
    connect_as(&L, 1, roomy, "");
    assert(sc_arena_reset(&scratch));
    assert(link_run(&L, &scratch, sc_str_from_lit(&scratch, (const uint8_t *)"SELECT\0 1", 9)));
    WAIT(&L, !active(&L), 5000);
    assert(L.ui.q == Q_ERROR && strcmp(read_term(&L).sqlstate, "22021") == 0);

    /* 300-byte rows, so the server's 8 KiB output buffer flushes about 25 of
     * them before the stall at row 60: fewer than the 100 that seal a first
     * chunk, so only the 250 ms tick can show them. */
    assert(link_run(&L, &scratch, str("SELECT g, repeat('x', 300) FROM generate_series(1, 60) g, "
                                      "LATERAL (SELECT pg_sleep(CASE WHEN g = 60 THEN 3 ELSE 0 END)) s")));
    uint64_t t0 = sc_clock_ms();
    WAIT(&L, L.ui.rows >= 20, 1500);
    assert(sc_clock_ms() - t0 < 1500 && active(&L));
    assert(link_run(&L, &scratch, str("SELECT 2")));
    assert(L.refused && L.refusal == CONN_REFUSE_BUSY);
    WAIT(&L, !active(&L), 5000);
    assert(L.ui.q == Q_ROWS && L.ui.rows == 60);
    run(&L, "SELECT 1");
    assert(L.ui.q == Q_ROWS && L.ui.rows == 1);
    close_link(&L);
    puts("ok NUL in SQL is an error; busy run refused; early rows flushed");
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
    assert(!read_term(&L).error); /* the cancel's own 57014 is not reported */
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
    /* A first set with COLUMNS and no rows, then a second set: the result
     * model must drop the first set's COLUMNS though no chunk count changed. */
    run(&L, "SELECT 1 AS a WHERE false; SELECT 2 AS b, 3 AS c");
    sc_str name;
    uint32_t oid = 0;
    assert(!L.broken && L.ui.q == Q_ROWS && L.res.cols == 2 && L.res.rows == 1);
    assert(result_column(&L.res, 0, &name, &oid) && name.len == 1 && name.ptr[0] == 'b' &&
           oid == 23);
    proto_cell bc;
    assert(result_cell(&L.res, 0, 1, &bc) && num(bc) == 3);
    close_link(&L);

    conn_limits limited = {4, 64, 10000};
    connect_as(&L, 1, limited, "");
    uint64_t t0 = sc_clock_ms();
    run(&L, "SELECT * FROM generate_series(1, 20000); SELECT 2 FROM pg_sleep(5)");
    assert(L.ui.q == Q_CAPPED && L.ui.cap == PROTO_CAP_LIMIT && L.ui.rows == 10000);
    assert(sc_clock_ms() - t0 < 4000); /* the later statement was cancelled */
    term_info capped = read_term(&L);
    /* The first statement may finish before the cancel lands; the second
     * (whose tag would be "SELECT 1") never does. */
    assert(!capped.error && capped.tags <= 1 && strcmp(capped.last_tag, "SELECT 1") != 0);
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
    /* libpq's parse errors quote the string: the password must not come back. */
    connect_as(&L, 1, roomy, "postgresql://explorer:hunter2SECRET@[::1");
    char msg[512];
    copy_str(msg, sizeof msg, L.failure);
    assert(L.ui.state == UI_CONN_UNREACHABLE && strstr(msg, "malformed") != NULL &&
           strstr(msg, "hunter2") == NULL);
    close_link(&L);
    puts("ok wrong password is auth; closed port is network; a malformed URI hides its password");
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

#define FAST "SELECT g, repeat('z', 90) FROM generate_series(1, 3000000) g"

/* A server that keeps the socket full: CLOSE mid-stream, and a capped result
 * whose remaining rows drain, both end within the 2 s close_link allows. */
static void fast_stream_close(void) {
    connect_as(&L, 1, roomy, "");
    assert(link_run(&L, &scratch, str(FAST)));
    WAIT(&L, L.ui.rows > 0, 5000);
    close_link(&L);

    const conn_limits capped = {4, 64, 10000};
    connect_as(&L, 1, capped, "");
    uint64_t t0 = sc_clock_ms();
    assert(link_run(&L, &scratch, str(FAST)));
    WAIT(&L, !active(&L), 5000);
    assert(L.ui.q == Q_CAPPED && L.ui.cap == PROTO_CAP_LIMIT && L.ui.rows == 10000);
    assert(sc_clock_ms() - t0 < 5000);
    close_link(&L);
    puts("ok CLOSE and a capped drain stay quick against a fast server");
}

/* Replies that would not fit their 256 KiB arena still go out: an error's
 * fields are cut, and tags past the room are dropped but counted. */
static void oversized_replies(void) {
    connect_as(&L, 1, roomy, "");
    run(&L, "DO $$BEGIN RAISE EXCEPTION '%', repeat('e', 300000); END$$");
    term_info t = read_term(&L);
    assert(L.ui.q == Q_ERROR && t.error && strcmp(t.sqlstate, "P0001") == 0);
    assert(t.message_len == 16384 && t.message[0] == 'e');

    static char sql[12000 * 9 + 1];
    for (int i = 0; i < 12000; i++)
        memcpy(sql + 9 * i, "SELECT 1;", 9);
    sql[sizeof sql - 1] = 0;
    run(&L, sql);
    t = read_term(&L);
    assert(L.ui.q == Q_ROWS && L.ui.earlier_tags == 11999 && t.tags > 1000 && t.tags < 12000);

    /* One row too big for an empty chunk, even with each cell cut to 4 KiB. */
    char *wide = sql;
    size_t n = (size_t)snprintf(wide, 16, "SELECT ");
    for (int i = 0; i < 70; i++)
        n += (size_t)snprintf(wide + n, 64, "%srepeat('x', 5000) AS c%d", i ? ", " : "", i);
    run(&L, wide);
    assert(L.ui.q == Q_CAPPED && L.ui.cap == PROTO_CAP_MEMORY && L.ui.rows == 0);
    run(&L, "SELECT 1");
    assert(L.ui.q == Q_ROWS && L.ui.rows == 1);
    close_link(&L);
    puts("ok long error cut; 12,000 statements; row wider than a chunk caps");
}

/* An idle connection that the server drops reports Disconnected. */
static void idle_loss(void) {
    sc_wake bell;
    assert(sc_wake_new(&bell).ok);
    connect_on(&L, 1, roomy, "", &bell);
    connect_as(&L2, 2, roomy, "");
    (void)sc_wake_drain(bell); /* from here on only L's LOST rings it */
    run(&L2, "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
             "WHERE application_name = 'explorer' AND pid <> pg_backend_pid()");
    close_link(&L2);
    /* The worker rings its bell for LOST: no polling of L until it does
     * (KTD6), which needs the flag that link_poll cleared at connect. */
    sc_result_size rung = sc_wake_wait(bell, 3000);
    assert(rung.ok && rung.value == 1);
    assert(link_poll(&L) && L.ui.state == UI_CONN_DISCONNECTED);
    close_link(&L);
    sc_wake_free(&bell);
    puts("ok idle connection dropped by the server is Disconnected, with a ring");
}

/* A black-holed host gives up at connect_timeout; a refused OPEN still joins. */
static void connect_timeout_and_refused_open(void) {
    uint64_t t0 = sc_clock_ms();
    connect_as(&L, 1, roomy, "host=10.255.255.1 connect_timeout=2");
    assert(L.ui.state == UI_CONN_UNREACHABLE && sc_clock_ms() - t0 < 5000);
    close_link(&L);

    static link_pool tiny; /* one control arena: too few to open */
    budget_split one = {1, 0, 0, 1};
    assert(link_pool_init(&tiny, one));
    assert(link_init(&L, &tiny, BUDGET_QUERY, 1, roomy, ring, &uiw));
    assert(link_open(&L, &scratch, str("")));
    assert(L.refused && L.refusal == CONN_REFUSE_POOL && L.ui.state == UI_CONN_NONE);
    t0 = sc_clock_ms();
    assert(!link_join(&L)); /* no BYE, but the worker returns */
    assert(sc_clock_ms() - t0 < 1000);
    link_free(&L);
    link_pool_free(&tiny);
    puts("ok connect_timeout honoured; refused OPEN joins");
}

/* ---- Tabs (plan U12) ---- */

/* One row every millisecond, for a minute and a half: always mid-stream. */
#define TAB_SLOW "SELECT g, md5(g::text) || left(pg_sleep(0.001)::text, 0) AS h " \
             "FROM generate_series(1, 90000) AS g"

static alignas(8) uint8_t book_mem[2][160u * 1024u];
static sc_arena books[2];

static result new_result(size_t i, size_t quota) {
    result r;
    assert(result_book_bytes(quota) <= sizeof book_mem[i]);
    sc_arena_init(&books[i], book_mem[i], sizeof book_mem[i]);
    assert(result_init(&r, &books[i], quota));
    return r;
}

/* Frees every arena a kept result holds; `shown` is the conn_ui copied
 * before link_keep. No link is needed: the arenas are the tab's. */
static void free_kept(result *kept, const conn_ui *shown) {
    alignas(8) static uint8_t mem[4096];
    sc_arena a;
    sc_arena_init(&a, mem, sizeof mem);
    sc_tslice out = SC_TSLICE_ALLOC(size_t, &a, 200);
    size_t n = result_close(kept, &a, out);
    assert(n == shown->held_count);
    for (size_t i = 0; i < n; i++) {
        size_t idx = 0;
        assert(SC_TSLICE_GET(size_t, &a, out, i, &idx));
        assert(link_pool_free_kept(&P, idx));
    }
    if (shown->terminal_kept) {
        assert(link_pool_free_kept(&P, shown->terminal));
    }
}

/* A finished result passes to its tab and stays readable while another tab
 * streams. Closing the streaming tab frees its arenas at once and its late
 * chunks on arrival; once the worker reports the end and the kept result is
 * freed, only the two reserved arenas are out. */
static void tabs_keep_and_close(void) {
    connect_as(&L, 30, roomy, "");
    link_adopt(&L, new_result(0, roomy.quota));
    run(&L, "SELECT g FROM generate_series(1, 5000) AS g");
    assert(L.ui.q == Q_ROWS && L.res.rows == 5000);
    conn_ui shown = L.ui;
    result kept;
    assert(link_keep(&L, &kept));
    assert(kept.rows == 5000 && L.res.rows == 0 && L.ui.held_count == 0);
    /* A run with no link_adopt fills the link's idle result, never the kept
     * one's book. */
    run(&L, "SELECT 'other' FROM generate_series(1, 300)");
    proto_cell c;
    assert(L.res.rows == 300 && result_cell(&kept, 4999, 0, &c) && c.bytes.len == 4 &&
           memcmp(c.bytes.ptr, "5000", 4) == 0);
    conn_ui other = L.ui;
    result idle_kept;
    assert(link_keep(&L, &idle_kept));
    free_kept(&idle_kept, &other);

    link_adopt(&L, new_result(1, roomy.quota));
    assert(link_run(&L, &scratch, str(TAB_SLOW)) && !L.refused);
    WAIT(&L, L.ui.held_count >= 3, 10000); /* COLUMNS and two chunks */
    assert(result_cell(&kept, 4999, 0, &c) && c.bytes.len == 4 && memcmp(c.bytes.ptr, "5000", 4) == 0);
    size_t out_before = link_pool_used(&P);
    assert(link_abandon(&L));
    assert(L.res.rows == 0 && L.ui.held_count == 0 && L.ui.q == Q_CANCELLING);
    assert(link_pool_used(&P) < out_before); /* freed at once */
    WAIT(&L, !active(&L), 5000);
    assert(L.ui.q == Q_CANCELLED && !L.ui.terminal_kept);
    free_kept(&kept, &shown);
    assert(link_pool_used(&P) == 2);
    /* The connection still runs queries. */
    link_adopt(&L, new_result(1, roomy.quota));
    run(&L, "SELECT 7");
    assert(L.ui.q == Q_ROWS && L.res.rows == 1);
    close_link(&L);
    puts("ok tab kept, streaming tab closed, pool back to its reserves");
}

/* Quitting with two connections streaming: both say BYE and join within 2 s. */
static void quit_while_streaming(void) {
    connect_as(&L, 31, roomy, "");
    connect_as(&L2, 32, roomy, "");
    assert(link_run(&L, &scratch, str(TAB_SLOW)) && link_run(&L2, &scratch, str(TAB_SLOW)));
    WAIT(&L, L.ui.rows > 0, 10000);
    WAIT(&L2, L2.ui.rows > 0, 10000);
    uint64_t t0 = sc_clock_ms();
    assert(link_close(&L) && link_close(&L2));
    while (L.ui.state != UI_CONN_CLOSED || L2.ui.state != UI_CONN_CLOSED) {
        assert(sc_clock_ms() - t0 < 2000);
        (void)sc_wake_wait(uiw, 50);
        (void)sc_wake_drain(uiw);
        (void)link_poll(&L);
        (void)link_poll(&L2);
        assert(!L.broken && !L2.broken);
    }
    assert(link_join(&L) && link_join(&L2));
    assert(sc_clock_ms() - t0 < 2000);
    link_free(&L);
    link_free(&L2);
    puts("ok quit with two connections streaming");
}

/* Runs `sql` with the limit lifted, polling until it ends. The pool never
 * runs dry under the link and the set stays within its quota. */
static void load_all(const char *sql, size_t quota) {
    assert(link_run_limit(&L, &scratch, str(sql), UINT64_MAX) && !L.refused);
    uint64_t end = sc_clock_ms() + 60000;
    while (active(&L)) {
        assert(sc_clock_ms() < end);
        (void)sc_wake_wait(uiw, 50);
        (void)sc_wake_drain(uiw);
        (void)link_poll(&L);
        assert(!L.broken && link_pool_used(&P) < P.book.count);
        assert(L.ui.held_count <= quota);
    }
}

#define MILLION "SELECT g FROM generate_series(1, 1000000) AS g"

/* "Load all", one million rows, in two tabs, then SELECT 1 in a third: the
 * first gets them all, the second caps for memory where growth runs out, and
 * the third still runs from the floor, which pays for one chunk (KTD19). */
static void million_rows(void) {
    const conn_limits app = {4, 128, 10000}; /* main.c's */
    connect_as(&L, 33, app, "");
    link_adopt(&L, new_result(0, app.quota));
    load_all(MILLION, app.quota);
    assert(L.ui.q == Q_ROWS && L.res.rows == 1000000);
    proto_cell c;
    assert(result_cell(&L.res, 999999, 0, &c) && c.bytes.len == 7 &&
           memcmp(c.bytes.ptr, "1000000", 7) == 0);
    printf("ok one million rows with the limit lifted: %zu chunk arenas of %zu\n",
           L.ui.held_count - 1u, app.quota);
    conn_ui first_ui = L.ui;
    result first;
    assert(link_keep(&L, &first));

    link_adopt(&L, new_result(1, app.quota));
    load_all(MILLION, app.quota);
    assert(L.ui.q == Q_CAPPED && L.ui.cap == PROTO_CAP_MEMORY && L.res.rows < 1000000);
    printf("ok a second million-row Load all caps for memory at %llu rows\n",
           (unsigned long long)L.res.rows);
    conn_ui second_ui = L.ui;
    result second;
    assert(link_keep(&L, &second));

    run(&L, "SELECT 1");
    assert(L.ui.q == Q_ROWS && L.res.rows == 1);
    /* With growth full, the floor pays for one chunk only: the first 100 rows. */
    run(&L, "SELECT g FROM generate_series(1, 20000) AS g");
    assert(L.ui.q == Q_CAPPED && L.ui.cap == PROTO_CAP_MEMORY && L.res.rows == 100);
    free_kept(&first, &first_ui);
    free_kept(&second, &second_ui);
    /* The tabs closed, the row limit comes back for the next run. */
    run(&L, "SELECT g FROM generate_series(1, 20000) AS g");
    assert(L.ui.q == Q_CAPPED && L.ui.cap == PROTO_CAP_LIMIT && L.res.rows == 10000);
    close_link(&L);
    puts("ok two Load-all tabs, then SELECT 1 in a third returns one row");
}

/* A result a tab keeps outlives its link: freeing the link leaves its arenas
 * out, a new link on the pool runs without touching them, and closing the
 * tab returns them. */
static void kept_result_outlives_link(void) {
    const conn_limits nine = {4, 9, 10000}; /* COLUMNS and eight chunks */
    connect_as(&L, 35, nine, "");
    link_adopt(&L, new_result(0, nine.quota));
    load_all(MILLION, nine.quota);
    assert(L.ui.q == Q_CAPPED && L.ui.cap == PROTO_CAP_QUOTA && L.ui.held_count == 9);
    conn_ui shown = L.ui;
    result kept;
    assert(link_keep(&L, &kept));
    uint64_t rows = kept.rows;
    close_link(&L); /* link_free */
    assert(link_pool_used(&P) == 10);
    connect_as(&L, 36, nine, "");
    run(&L, "SELECT 1");
    assert(L.ui.q == Q_ROWS && L.res.rows == 1);
    close_link(&L);
    assert(link_pool_used(&P) == 10 && kept.rows == rows);
    proto_cell c;
    assert(result_cell(&kept, 0, 0, &c) && c.bytes.len == 1 && c.bytes.ptr[0] == '1');
    free_kept(&kept, &shown); /* the tab closes */
    assert(link_pool_used(&P) == 0);
    puts("ok link_free leaves a tab's 10 arenas; closing the tab frees them");
}

/* A window past 64: the worker's END step returns every unused grant, more
 * actions than a fixed list of 64 held, and the link broke. The worker sizes
 * its action list and handle table from the limits (U17). */
static void wide_window(void) {
    const conn_limits wide = {70, 128, 10000};
    connect_as(&L, 34, wide, "");
    run(&L, "SELECT 1");
    assert(L.ui.q == Q_ROWS && L.ui.rows == 1);
    close_link(&L);
    puts("ok a window of 70 returns its grants in one step");
}

/* ---- Cancel, disconnect and reconnect (plan U13) ---- */

/* Polls retiring link `r` until its worker is done, then joins and frees it. */
static void retired_ends(conn_link *r, uint64_t ms) {
    uint64_t end = sc_clock_ms() + ms;
    while (!link_ended(r)) {
        assert(sc_clock_ms() < end);
        (void)sc_wake_wait(uiw, 50);
        (void)sc_wake_drain(uiw);
        (void)link_poll(r);
    }
    assert(r->ui.state == UI_CONN_CLOSED && !r->broken);
    assert(link_join(r));
    link_free(r);
}

/* Rows that stream, 300 bytes each so the server flushes them as they go,
 * then a 30 s sleep before row `n` + 1. */
#define ROWS_THEN_SLEEP(n)                                                                         \
    "SELECT g, repeat('x', 300) AS pad, CASE WHEN g = " #n " + 1 THEN pg_sleep(30) END AS s "      \
    "FROM generate_series(1, " #n " + 1) AS g"

/* Disconnect during a run returns control at once: the run ends Disconnected
 * with its rows kept, a new link on the pool runs SELECT 1, and quitting
 * afterwards (the old link's BYE, the new one's close) takes under 2 s. The
 * old link's arenas are the retiring owner's until they come home. */
static void disconnect_then_reconnect(void) {
    connect_as(&L, 40, roomy, "");
    link_adopt(&L, new_result(0, roomy.quota));
    assert(link_run(&L, &scratch, str(ROWS_THEN_SLEEP(500))) && !L.refused);
    WAIT(&L, L.ui.rows >= 300, 10000);
    uint64_t t0 = sc_clock_ms();
    result kept;
    conn_ui shown = L.ui; /* before link_retire, which forgets the arenas */
    assert(link_retire(&L, &kept));
    assert(sc_clock_ms() - t0 < 100);
    shown.q = L.ui.q;
    assert(shown.q == Q_DISCONNECTED && shown.rows >= 300 && kept.rows == shown.rows);
    assert(budget_held(&P.budget, BUDGET_RETIRING) > 0); /* the terminal, grants, control */
    assert(budget_held(&P.budget, BUDGET_QUERY) == shown.held_count);
    retired_ends(&L, 2000);
    assert(budget_held(&P.budget, BUDGET_RETIRING) == 0);
    assert(link_pool_used(&P) == shown.held_count); /* only the tab's rows */
    /* Reconnect: a new link with the same settings. */
    connect_as(&L, 41, roomy, "");
    run(&L, "SELECT 1");
    assert(L.ui.q == Q_ROWS && L.ui.rows == 1);
    proto_cell c;
    assert(result_cell(&kept, shown.rows - 1u, 0, &c) && num(c) == shown.rows);
    uint64_t quit = sc_clock_ms();
    close_link(&L);
    assert(sc_clock_ms() - quit < 2000);
    free_kept(&kept, &shown);
    puts("ok Disconnect mid-run keeps the rows; Reconnect runs SELECT 1; quit under 2 s");
}

/* The server ends the worker's backend after 1,000 rows: LOST, with all
 * 1,000 kept. Reconnect retires the lost link and opens a new one; the tab
 * still reads its rows, and a new run succeeds. */
static void lost_then_reconnect(void) {
    connect_as(&L, 42, roomy, "");
    link_adopt(&L, new_result(0, roomy.quota));
    assert(link_run(&L, &scratch, str(ROWS_THEN_SLEEP(1000))) && !L.refused);
    WAIT(&L, L.ui.rows >= 900, 10000);
    connect_as(&L2, 43, roomy, "");
    run(&L2, "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
             "WHERE application_name = 'explorer' AND pid <> pg_backend_pid()");
    close_link(&L2);
    WAIT(&L, L.ui.state == UI_CONN_DISCONNECTED, 5000);
    assert(L.ui.q == Q_DISCONNECTED && L.ui.rows == 1000);
    result kept;
    conn_ui shown = L.ui;
    assert(link_retire(&L, &kept));
    retired_ends(&L, 2000);
    connect_as(&L, 44, roomy, "");
    run(&L, "SELECT 2");
    assert(L.ui.q == Q_ROWS && L.ui.rows == 1);
    close_link(&L);
    assert(kept.rows == 1000);
    proto_cell c;
    for (uint64_t r = 0; r < 1000; r++) {
        assert(result_cell(&kept, r, 0, &c) && num(c) == r + 1);
    }
    free_kept(&kept, &shown);
    puts("ok LOST after 1,000 rows keeps them across Reconnect; a new run succeeds");
}

/* Cancel while connecting to a host that never answers retires the link at
 * once: Disconnected, Reconnect offered. The worker closes its connect and
 * says BYE soon after. */
static void cancel_while_connecting(void) {
    assert(link_init(&L, &P, BUDGET_QUERY, 45, roomy, ring, &uiw));
    assert(link_open(&L, &scratch, str("host=10.255.255.1 connect_timeout=30")));
    assert(link_run(&L, &scratch, str("SELECT 1")) && L.ui.q == Q_QUEUED);
    (void)sc_wake_wait(uiw, 200);
    (void)link_poll(&L);
    assert(L.ui.state == UI_CONN_CONNECTING);
    uint64_t t0 = sc_clock_ms();
    result kept;
    assert(conn_pool_can_retire(&P.book) && link_retire(&L, &kept));
    assert(sc_clock_ms() - t0 < 100 && L.ui.q == Q_DISCONNECTED);
    assert(conn_pool_can_retire(&P.book)); /* one slot left: Reconnect is offered */
    retired_ends(&L, 2000);
    puts("ok Cancel while connecting to a black hole is Disconnected at once");
}

/* Two Reconnects against the black hole fill both retiring slots; a third is
 * refused until one says BYE. */
static void retiring_slots(void) {
    const char *hole = "host=10.255.255.1 connect_timeout=30";
    result kept;
    assert(link_init(&R, &P, BUDGET_QUERY, 46, roomy, ring, &uiw) && link_open(&R, &scratch, str(hole)));
    assert(link_retire(&R, &kept));
    assert(link_init(&R2, &P, BUDGET_QUERY, 47, roomy, ring, &uiw) && link_open(&R2, &scratch, str(hole)));
    assert(link_retire(&R2, &kept));
    assert(link_init(&L, &P, BUDGET_QUERY, 48, roomy, ring, &uiw) && link_open(&L, &scratch, str(hole)));
    assert(!conn_pool_can_retire(&P.book));
    assert(!link_retire(&L, &kept) && L.refused && L.refusal == CONN_REFUSE_RETIRING);
    assert(L.ui.state == UI_CONN_CONNECTING && !L.retired);
    retired_ends(&R, 2000);
    assert(conn_pool_can_retire(&P.book) && link_retire(&L, &kept));
    retired_ends(&R2, 2000);
    retired_ends(&L, 2000);
    puts("ok two retiring links fill the slots; a third waits for a BYE");
}

/* A link freed without a BYE (its worker told to return mid-stream) gives
 * back every arena it took: the U17 open item. */
static void free_without_bye(void) {
    connect_as(&L, 49, roomy, "");
    assert(link_run(&L, &scratch, str(TAB_SLOW)) && !L.refused);
    WAIT(&L, L.ui.held_count >= 2, 10000);
    assert(link_pool_used(&P) > 2);
    assert(!link_join(&L)); /* no BYE: the closed request channel makes it return */
    link_free(&L);
    assert(link_pool_used(&P) == 0 && budget_held(&P.budget, BUDGET_QUERY) == 0);
    puts("ok a link that never said BYE gives every arena back when freed");
}

/* ---- Over TCP, through a relay (plan U13, KTD7) ---- */

/* A TCP relay to the test server on 127.0.0.1:$PGPORT. Its first connection
 * is the query connection, forwarded both ways at once; every later one is a
 * cancel connection, forwarded only after `cancel_delay_ms`, or never
 * (UINT32_MAX): it is accepted and left unanswered. */
#define RELAY_CONNS 8

typedef struct {
    int listen_fd;
    uint16_t port;
    uint32_t cancel_delay_ms;
    atomic_bool stop;
    pthread_t thread;
    int down[RELAY_CONNS], up[RELAY_CONNS]; /* client side, server side (-1: not yet) */
    uint64_t since[RELAY_CONNS];
    size_t count;
} relay;

static relay RL;

static int upstream(void) {
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)atoi(getenv("PGPORT")));
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0 && connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0);
    return fd;
}

/* Copies what `from` has to `to`; false once either side is done. */
static bool pass(int from, int to) {
    char buf[16384];
    ssize_t n = read(from, buf, sizeof buf);
    for (ssize_t off = 0; n > 0 && off < n;) {
        ssize_t w = write(to, buf + off, (size_t)(n - off));
        if (w <= 0)
            return false;
        off += w;
    }
    return n > 0;
}

static void *relay_main(void *arg) {
    relay *r = arg;
    while (!atomic_load(&r->stop)) {
        struct pollfd fds[1 + 2 * RELAY_CONNS];
        size_t which[1 + 2 * RELAY_CONNS];
        size_t n = 0;
        fds[n].fd = r->listen_fd;
        fds[n].events = POLLIN;
        which[n++] = SIZE_MAX;
        for (size_t i = 0; i < r->count; i++) {
            if (r->up[i] < 0 && r->down[i] >= 0 && r->cancel_delay_ms != UINT32_MAX &&
                sc_clock_ms() - r->since[i] >= r->cancel_delay_ms) {
                r->up[i] = upstream();
            }
            if (r->up[i] >= 0) {
                fds[n].fd = r->down[i];
                fds[n].events = POLLIN;
                which[n++] = 2 * i;
                fds[n].fd = r->up[i];
                fds[n].events = POLLIN;
                which[n++] = 2 * i + 1;
            }
        }
        if (poll(fds, (nfds_t)n, 20) <= 0)
            continue;
        for (size_t k = 0; k < n; k++) {
            if ((fds[k].revents & (POLLIN | POLLHUP | POLLERR)) == 0)
                continue;
            if (which[k] == SIZE_MAX) {
                int fd = accept(r->listen_fd, NULL, NULL);
                assert(fd >= 0 && r->count < RELAY_CONNS);
                r->down[r->count] = fd;
                r->up[r->count] = r->count == 0 ? upstream() : -1;
                r->since[r->count] = sc_clock_ms();
                r->count++;
                continue;
            }
            size_t i = which[k] / 2;
            if (r->up[i] < 0)
                continue; /* closed by its partner earlier in this pass */
            bool ok = which[k] % 2 == 0 ? pass(r->down[i], r->up[i]) : pass(r->up[i], r->down[i]);
            if (!ok) {
                close(r->down[i]);
                close(r->up[i]);
                r->down[i] = -1;
                r->up[i] = -1;
            }
        }
    }
    for (size_t i = 0; i < r->count; i++) {
        if (r->down[i] >= 0)
            close(r->down[i]);
        if (r->up[i] >= 0)
            close(r->up[i]);
    }
    close(r->listen_fd);
    return NULL;
}

static void relay_start(relay *r, uint32_t cancel_delay_ms) {
    memset(r, 0, sizeof *r);
    r->cancel_delay_ms = cancel_delay_ms;
    atomic_store(&r->stop, false);
    r->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof sa;
    assert(r->listen_fd >= 0 && bind(r->listen_fd, (struct sockaddr *)&sa, sizeof sa) == 0);
    assert(listen(r->listen_fd, 8) == 0);
    assert(getsockname(r->listen_fd, (struct sockaddr *)&sa, &len) == 0);
    r->port = ntohs(sa.sin_port);
    assert(pthread_create(&r->thread, NULL, relay_main, r) == 0);
}

static void relay_stop(relay *r) {
    atomic_store(&r->stop, true);
    assert(pthread_join(r->thread, NULL) == 0);
}

static char relay_info[64];

static const char *through(const relay *r) {
    snprintf(relay_info, sizeof relay_info, "host=127.0.0.1 port=%u", (unsigned)r->port);
    return relay_info;
}

/* Waits ~`ms` while the link runs, reading its replies. */
static void idle_for(conn_link *l, uint64_t ms) {
    uint64_t t0 = sc_clock_ms();
    while (sc_clock_ms() - t0 < ms) {
        (void)sc_wake_wait(uiw, 20);
        (void)sc_wake_drain(uiw);
        (void)link_poll(l);
    }
}

/* A cancel still under way when its query ends on its own must not land on
 * the next run on the link (KTD7). The relay holds the cancel back for 1 s:
 * the first query ends at 0.3 s, and the next, 2 s long, waits for the cancel
 * to go through before it is sent, so it returns its row. */
static void late_cancel_spares_next_run(void) {
    relay_start(&RL, 1000);
    connect_as(&L, 50, roomy, through(&RL));
    assert(L.ui.state == UI_CONN_CONNECTED);
    assert(link_run(&L, &scratch, str("SELECT pg_sleep(0.3)")) && !L.refused);
    idle_for(&L, 50);
    assert(link_cancel(&L));
    WAIT(&L, !active(&L), 5000);
    /* It ended on its own, its row dropped, before the cancel arrived. */
    assert(L.ui.q == Q_CANCELLED && !read_term(&L).error);
    run(&L, "SELECT 1 FROM pg_sleep(2)");
    assert(L.ui.q == Q_ROWS && L.ui.rows == 1);
    close_link(&L);
    relay_stop(&RL);
    puts("ok a late cancel waits out its query and spares the next run");
}

#ifdef LIBPQ_HAS_ASYNC_CANCEL
/* libpq 17+: a cancel whose connection is never answered fails at the
 * adapter's deadline (SC_PQ_CANCEL_DEADLINE_MS, shortened for this build).
 * The worker reports it: "cancel failed" and Disconnect offered, before the
 * UI's own deadline (never ticked here), and Disconnect then retires the link. */
static void cancel_never_answered(void) {
    relay_start(&RL, UINT32_MAX);
    connect_as(&L, 52, roomy, through(&RL));
    assert(L.ui.state == UI_CONN_CONNECTED);
    assert(link_run(&L, &scratch, str("SELECT pg_sleep(30)")) && !L.refused);
    idle_for(&L, 200);
    uint64_t t0 = sc_clock_ms();
    assert(link_cancel(&L) && L.ui.q == Q_CANCELLING);
    WAIT(&L, L.ui.cancel_failed, SC_PQ_CANCEL_DEADLINE_MS + 3000);
    uint64_t took = sc_clock_ms() - t0;
    assert(took >= SC_PQ_CANCEL_DEADLINE_MS && took < CONN_CANCEL_DEADLINE_MS);
    assert(L.ui.offer_disconnect && L.ui.q == Q_CANCELLING);
    result kept;
    assert(link_retire(&L, &kept));
    retired_ends(&L, 2000);
    relay_stop(&RL);
    printf("ok a cancel never answered fails after %llu ms; Disconnect retires the link\n",
           (unsigned long long)took);
}
#endif

/* The adapter's TCP keepalives reach libpq (plan Approach 7): the live
 * connection holds them, whatever the conninfo says. */
static void keepalives_reach_libpq(void) {
    char info[96];
    snprintf(info, sizeof info, "host=127.0.0.1 port=%s keepalives=0 keepalives_idle=7200",
             getenv("PGPORT"));
    sc_pq pq;
    assert(sc_pq_connect_start(&pq, &scratch, str(info), SC_PQ_QUERY).ok);
    bool want_write = true;
    sc_pq_connect_status st = SC_PQ_CONNECTING;
    uint64_t end = sc_clock_ms() + 10000;
    while (st == SC_PQ_CONNECTING) {
        assert(sc_clock_ms() < end);
        sc_result_size r = sc_pq_wait(pq, uiw, want_write, 50);
        (void)sc_wake_drain(uiw);
        st = sc_pq_connect_poll(pq, r.ok && (r.value & 1u) != 0, &want_write);
    }
    assert(st == SC_PQ_CONNECTED);
    const char *keys[] = {"keepalives", "keepalives_idle", "keepalives_interval", "keepalives_count"};
    const char *want[] = {"1", "10", "5", "3"};
    for (int i = 0; i < 4; i++) {
        assert(sc_arena_reset(&scratch));
        sc_str k = sc_str_from_lit(&scratch, (const uint8_t *)keys[i], strlen(keys[i]));
        sc_buf b = sc_buf_new(sc_arena_alloc(&scratch, 64));
        assert(sc_pq_put_option(pq, &scratch, k, &scratch, &b));
        sc_str v = sc_buf_view(&scratch, b);
        uint64_t n = 0;
        assert(v.len >= 8);
        for (int j = 0; j < 8; j++)
            n |= (uint64_t)v.ptr[j] << (8 * j);
        assert(n == strlen(want[i]) && v.len == 8 + n && memcmp(v.ptr + 8, want[i], n) == 0);
    }
    sc_pq_finish(&pq);
    puts("ok keepalives 1, idle 10, interval 5, count 3 reach libpq over TCP");
}

/* Polls `l` until it is broken, failing after `ms`. */
static void until_broken(conn_link *l, uint64_t ms) {
    uint64_t end = sc_clock_ms() + ms;
    while (!l->broken) {
        assert(sc_clock_ms() < end);
        (void)sc_wake_wait(uiw, 50);
        (void)sc_wake_drain(uiw);
        (void)link_poll(l);
    }
}

/* Polls retired link `r` until its worker is done, then joins and frees it,
 * BYE or not. */
static void retired_returns(conn_link *r, uint64_t ms) {
    uint64_t end = sc_clock_ms() + ms;
    while (!link_ended(r)) {
        assert(sc_clock_ms() < end);
        (void)sc_wake_wait(uiw, 50);
        (void)sc_wake_drain(uiw);
        (void)link_poll(r);
    }
    (void)link_join(r);
    link_free(r);
}

/* Streams rows into a tab's result on `l`, then makes its worker return
 * without BYE (the request channel closed under it): the link breaks
 * mid-run, and its run reads Disconnected. */
static void break_mid_run(conn_link *l, uint32_t id) {
    connect_as(l, id, roomy, "");
    link_adopt(l, new_result(0, roomy.quota));
    assert(link_run(l, &scratch, str(TAB_SLOW)) && !l->refused);
    WAIT(l, l->ui.held_count >= 3, 10000);
    sc_chan_close(&l->requests);
    (void)sc_wake_ring(l->wake);
    until_broken(l, 5000);
    assert(l->ui.q == Q_DISCONNECTED);
}

/* The arenas of the set `l` holds, read before the link is freed. */
static size_t held_now(const conn_link *l, size_t *idx, size_t cap) {
    assert(l->ui.held_count <= cap);
    for (size_t i = 0; i < l->ui.held_count; i++) {
        idx[i] = conn_ui_held(&l->ui, i);
    }
    return l->ui.held_count;
}

/* The tab's rows are still its own: kept in the book and readable. */
static void rows_survive(const result *kept, const size_t *idx, size_t n) {
    for (size_t i = 0; i < n; i++) {
        assert(conn_pool_owner(&P.book, idx[i]) == POOL_KEPT);
    }
    proto_cell c;
    assert(kept->rows > 0 && result_cell(kept, kept->rows - 1u, 0, &c) && num(c) == kept->rows);
}

/* A link that breaks while a tab holds its rows passes them on as a live link
 * does, whether the tab keeps them first (settle) or Reconnect retires the
 * link at once. Freeing the link then reclaims only its own arenas, and the
 * tab frees exactly the ones it kept. */
static void broken_link_keeps_rows(void) {
    size_t idx[64];
    break_mid_run(&L, 53);
    conn_ui shown = L.ui;
    size_t n = held_now(&L, idx, 64);
    result kept;
    assert(link_keep(&L, &kept));
    result none;
    assert(link_retire(&L, &none));
    retired_returns(&L, 3000);
    rows_survive(&kept, idx, n);
    assert(link_pool_used(&P) == n);
    free_kept(&kept, &shown);

    break_mid_run(&L, 54);
    shown = L.ui;
    n = held_now(&L, idx, 64);
    assert(link_retire(&L, &kept));
    retired_returns(&L, 3000);
    rows_survive(&kept, idx, n);
    free_kept(&kept, &shown);
    puts("ok a broken link's rows stay the tab's through Reconnect and its reclaim");
}

/* A retired link that breaks before its CLOSE could go out (the control arena
 * still away with a CANCEL) lets its worker go, so the link ends and its
 * retiring slot comes back. The break is set by hand, as a failed step sets it. */
static void retired_link_breaks(void) {
    connect_as(&L, 55, roomy, "");
    link_adopt(&L, new_result(0, roomy.quota));
    assert(link_run(&L, &scratch, str(TAB_SLOW)) && !L.refused);
    WAIT(&L, L.ui.held_count >= 2, 10000);
    assert(link_cancel(&L) && !L.ui.ctl_home);
    conn_ui shown = L.ui;
    result kept;
    assert(link_retire(&L, &kept) && L.ui.close_pending);
    L.broken = true;
    retired_returns(&L, 3000);
    assert(P.book.retiring == 0);
    free_kept(&kept, &shown);
    puts("ok a retired link that breaks before its CLOSE still lets its worker go");
}

/* ---- The link set (links.c): retire, reconnect, drain and quit (plan U13) ---- */

static link_set LS;
static alignas(8) uint8_t info_mem[256];
static sc_arena info_a;

/* `s` as the set's connection settings, for the links it opens next. */
static sc_str set_info(const char *s) {
    sc_arena_init(&info_a, info_mem, sizeof info_mem);
    return s[0] == 0 ? (sc_str){NULL, 0, info_a.epoch}
                     : sc_str_from_lit(&info_a, (const uint8_t *)s, strlen(s));
}

static void wait_bell(void *arg, uint32_t ms) {
    (void)sc_wake_wait(*(sc_wake *)arg, ms);
    (void)sc_wake_drain(*(sc_wake *)arg);
}

/* Polls the set's current link until `cond` holds, failing after `ms`. The
 * retired links are left alone, so their slots stay taken. */
#define CUR_WAIT(cond, ms)                                                                         \
    do {                                                                                           \
        uint64_t end_ = sc_clock_ms() + (ms);                                                      \
        while (!(cond)) {                                                                          \
            if (sc_clock_ms() > end_) {                                                            \
                fprintf(stderr, "timed out at line %d: %s\n", __LINE__, #cond);                    \
                abort();                                                                           \
            }                                                                                      \
            wait_bell(&uiw, 50);                                                                   \
            (void)link_poll(links_cur(&LS));                                                       \
        }                                                                                          \
    } while (0)

/* Drains the retired links until the current one can retire, failing after `ms`. */
static void drain_until_free(uint64_t ms) {
    uint64_t end = sc_clock_ms() + ms;
    while (!links_can_retire(&LS)) {
        assert(sc_clock_ms() < end);
        wait_bell(&uiw, 50);
        links_drain(&LS);
    }
}

#define FAILS_FAST "host=127.0.0.1 port=1 connect_timeout=2"
#define BLACK_HOLE "host=10.255.255.1 connect_timeout=30"

/* A connect that fails keeps its queued run, and Reconnect hands it back as
 * never sent. Two Reconnects fill both retiring slots, and the third link,
 * still connecting to a host that never answers, cannot retire: Disconnect is
 * refused and Cancel cancels its queued run instead. Disconnected twice while
 * the links drain, Reconnect is refused, so no link opens that could not be
 * stopped; once a retired link ends it works again. */
static void link_set_retiring_slots(void) {
    link_handoff h;
    bool handed = false;
    links_init(&LS, &P, roomy, ring, &uiw, &info_a, set_info(FAILS_FAST));
    assert(links_open(&LS));
    conn_link *a = links_cur(&LS);
    assert(link_run(a, &scratch, str("SELECT 1")) && a->ui.q == Q_QUEUED);
    CUR_WAIT(a->ui.state == UI_CONN_UNREACHABLE, 5000);
    assert(a->ui.q == Q_QUEUED);
    assert(links_reconnect(&LS, &h, &handed) && handed);
    assert(h.unsent && h.ended.q == Q_DISCONNECTED && links_cur(&LS) != a);
    CUR_WAIT(links_cur(&LS)->ui.state == UI_CONN_UNREACHABLE, 5000);
    LS.info = set_info(BLACK_HOLE);
    assert(links_reconnect(&LS, &h, &handed) && handed && !h.unsent);
    conn_link *c = links_cur(&LS);
    assert(c->ui.state == UI_CONN_CONNECTING && !links_can_retire(&LS));
    assert(link_run(c, &scratch, str("SELECT 1")) && c->ui.q == Q_QUEUED);
    assert(!links_disconnect(&LS, &h));
    assert(links_cancel_connecting(&LS, &h, &handed) && !handed);
    assert(links_cur(&LS) == c && c->ui.q == Q_CANCELLED && c->ui.state == UI_CONN_CONNECTING);

    drain_until_free(3000);
    assert(links_disconnect(&LS, &h) && LS.offline);
    assert(links_reconnect(&LS, &h, &handed) && !handed && !LS.offline);
    assert(links_disconnect(&LS, &h) && LS.offline && !links_can_retire(&LS));
    assert(!links_reconnect(&LS, &h, &handed) && !handed && !links_cur(&LS)->started);
    drain_until_free(3000);
    assert(links_reconnect(&LS, &h, &handed) && !handed && !LS.offline);
    assert(links_cur(&LS)->ui.state == UI_CONN_CONNECTING);
    uint64_t t0 = sc_clock_ms();
    assert(links_close(&LS, 2000, wait_bell, &uiw) && sc_clock_ms() - t0 < 2000);
    puts("ok a failed connect keeps its run; a third link waits for a slot; Cancel falls back");
}

/* The server ends the link's backend under 1,000 rows: Reconnect hands the
 * rows over, still readable, and a tab that waited queues on the opening link
 * and returns its row once it connects. */
static void link_set_reconnect_after_lost(void) {
    links_init(&LS, &P, roomy, ring, &uiw, &info_a, set_info(""));
    assert(links_open(&LS));
    conn_link *a = links_cur(&LS);
    CUR_WAIT(a->ui.state == UI_CONN_CONNECTED, 10000);
    link_adopt(a, new_result(0, roomy.quota));
    assert(link_run(a, &scratch, str(ROWS_THEN_SLEEP(1000))) && !a->refused);
    CUR_WAIT(a->ui.rows >= 900, 10000);
    connect_as(&L2, 60, roomy, "");
    run(&L2, "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
             "WHERE application_name = 'explorer' AND pid <> pg_backend_pid()");
    close_link(&L2);
    CUR_WAIT(a->ui.state == UI_CONN_DISCONNECTED, 5000);
    link_handoff h;
    bool handed = false;
    assert(links_reconnect(&LS, &h, &handed) && handed);
    assert(h.ended.q == Q_DISCONNECTED && !h.unsent && h.kept.rows == 1000);
    conn_link *b = links_cur(&LS);
    assert(b != a && b->ui.state == UI_CONN_CONNECTING);
    assert(link_run(b, &scratch, str("SELECT 2")) && b->ui.q == Q_QUEUED);
    CUR_WAIT(!active(b), 10000);
    assert(b->ui.q == Q_ROWS && b->ui.rows == 1);
    proto_cell c;
    assert(result_cell(&h.kept, 999, 0, &c) && num(c) == 1000);
    assert(links_close(&LS, 2000, wait_bell, &uiw));
    free_kept(&h.kept, &h.ended);
    puts("ok Reconnect after LOST keeps 1,000 rows; a waiting run goes out once it connects");
}

/* While set, a worker that rings the UI blocks there: a worker stuck past the
 * quit's 2 s, as one in a blocking cancel or DNS lookup is. */
static atomic_bool stall;

static void stall_ring(void *arg) {
    while (atomic_load(&stall)) {
        struct timespec t = {0, 1000000};
        (void)nanosleep(&t, NULL);
    }
    ring(arg);
}

/* Quit with a current link whose worker returned without BYE joins it at
 * once; quit with a retiring link whose worker is stuck returns after the 2 s
 * and leaves that link, which a later close joins once the worker moves. */
static void link_set_close(void) {
    links_init(&LS, &P, roomy, ring, &uiw, &info_a, set_info(""));
    assert(links_open(&LS));
    conn_link *a = links_cur(&LS);
    CUR_WAIT(a->ui.state == UI_CONN_CONNECTED, 10000);
    link_adopt(a, new_result(0, roomy.quota));
    assert(link_run(a, &scratch, str(TAB_SLOW)) && !a->refused);
    CUR_WAIT(a->ui.held_count >= 2, 10000);
    sc_chan_close(&a->requests);
    (void)sc_wake_ring(a->wake);
    CUR_WAIT(a->broken, 5000);
    conn_ui shown = a->ui; /* the tab keeps the rows, as settle has it */
    result kept;
    assert(link_keep(a, &kept));
    uint64_t t0 = sc_clock_ms();
    assert(links_close(&LS, 2000, wait_bell, &uiw) && sc_clock_ms() - t0 < 2000);
    assert(link_pool_used(&P) == shown.held_count);
    free_kept(&kept, &shown);

    links_init(&LS, &P, roomy, stall_ring, &uiw, &info_a, set_info(""));
    assert(links_open(&LS));
    CUR_WAIT(links_cur(&LS)->ui.state == UI_CONN_CONNECTED, 10000);
    atomic_store(&stall, true);
    link_handoff h;
    assert(links_disconnect(&LS, &h));
    t0 = sc_clock_ms();
    assert(!links_close(&LS, 2000, wait_bell, &uiw));
    uint64_t took = sc_clock_ms() - t0;
    assert(took >= 2000 && took < 2500);
    atomic_store(&stall, false);
    assert(links_close(&LS, 2000, wait_bell, &uiw));
    printf("ok quit joins a link without BYE; a stuck retiring link returns quit in %llu ms\n",
           (unsigned long long)took);
}

/* ---- The metadata link and the catalog (plan U18, KTD21) ---- */

static meta_side M;
static alignas(8) uint8_t cat_mem[1u << 20], keep_mem[1u << 14], msql_mem[4096];
static sc_arena cat_a, keep_a, msql_a;

/* Polls the set's current link, the metadata link and the retired ones until
 * `cond` holds, failing after `ms`. */
#define META_WAIT(cond, ms)                                                                        \
    do {                                                                                           \
        uint64_t end_ = sc_clock_ms() + (ms);                                                      \
        while (!(cond)) {                                                                          \
            if (sc_clock_ms() > end_) {                                                            \
                fprintf(stderr, "timed out at line %d: %s\n", __LINE__, #cond);                    \
                abort();                                                                           \
            }                                                                                      \
            wait_bell(&uiw, 50);                                                                   \
            (void)link_poll(links_cur(&LS));                                                       \
            (void)meta_poll(&M);                                                                   \
            links_drain(&LS);                                                                      \
        }                                                                                          \
    } while (0)

/* A link set on `info` with its metadata driver, connected, its schema list
 * read. */
static void meta_start(const char *info) {
    links_init(&LS, &P, roomy, ring, &uiw, &info_a, set_info(info));
    sc_arena_init(&cat_a, cat_mem, sizeof cat_mem);
    sc_arena_init(&keep_a, keep_mem, sizeof keep_mem);
    sc_arena_init(&msql_a, msql_mem, sizeof msql_mem);
    assert(meta_init(&M, &LS, &cat_a, &keep_a, &msql_a));
    assert(links_open(&LS));
    META_WAIT(meta_shown(&M) == CATALOG_SHOW_READY, 10000);
}

static size_t schema_named(const char *name) {
    catalog_schema s;
    for (size_t i = 0; catalog_schema_at(&M.cat, i, &s); i++) {
        if (sc_str_eq_lit(&cat_a, s.name, (const uint8_t *)name, strlen(name))) {
            return i;
        }
    }
    fprintf(stderr, "no schema %s\n", name);
    abort();
}

static catalog_schema schema_at(size_t i) {
    catalog_schema s;
    assert(catalog_schema_at(&M.cat, i, &s));
    return s;
}

static bool schema_done(size_t i) {
    return schema_at(i).load == CATALOG_LOAD_DONE;
}

/* Schema `si`'s relation `name`. */
static catalog_relation rel_named(size_t si, const char *name) {
    catalog_schema s = schema_at(si);
    catalog_relation r;
    for (size_t i = 0; catalog_relation_at(&M.cat, &s, i, &r); i++) {
        if (sc_str_eq_lit(&cat_a, r.name, (const uint8_t *)name, strlen(name))) {
            return r;
        }
    }
    fprintf(stderr, "no relation %s\n", name);
    abort();
}

static bool has_rel(size_t si, const char *name) {
    catalog_schema s = schema_at(si);
    catalog_relation r;
    for (size_t i = 0; catalog_relation_at(&M.cat, &s, i, &r); i++) {
        if (sc_str_eq_lit(&cat_a, r.name, (const uint8_t *)name, strlen(name))) {
            return true;
        }
    }
    return false;
}

static bool is(sc_str s, const char *want) {
    return sc_str_eq_lit(&cat_a, s, (const uint8_t *)want, strlen(want));
}

/* Loads relation `name`'s detail in schema `si`. */
static catalog_relation detail_of(size_t si, const char *name) {
    catalog_relation r = rel_named(si, name);
    assert(catalog_want_detail(&M.cat, schema_at(si).oid, r.oid));
    META_WAIT(rel_named(si, name).detail == CATALOG_LOAD_DONE, 10000);
    return rel_named(si, name);
}

static catalog_column column_at(const catalog_relation *r, size_t i) {
    catalog_column c;
    assert(catalog_column_at(&M.cat, r, i, &c));
    return c;
}

static catalog_constraint constraint_at(const catalog_relation *r, size_t i) {
    catalog_constraint k;
    assert(catalog_constraint_at(&M.cat, r, i, &k));
    return k;
}

/* The sidebar's error reads `want`, with Retry offered when `retry`. */
static bool sidebar_says(const char *want, bool retry) {
    alignas(8) static uint8_t fa_mem[1024];
    sc_arena fa;
    sc_arena_init(&fa, fa_mem, sizeof fa_mem);
    const sc_arena *a = NULL;
    sc_str text;
    bool can = !retry;
    return meta_sidebar_error(&M, &fa, &a, &text, &can) && can == retry &&
           sc_str_eq_lit(a, text, (const uint8_t *)want, strlen(want));
}

/* Constraint `k`'s columns are attnum `a`, then `b` unless it is 0. */
static bool attnums_are(const catalog_constraint *k, uint32_t a, uint32_t b) {
    uint32_t x = 0, y = 0;
    size_t n = b == 0 ? 1u : 2u;
    return sc_tslice_len(k->columns) == n && SC_TSLICE_GET(uint32_t, &cat_a, k->columns, 0, &x) &&
           x == a && (b == 0 || (SC_TSLICE_GET(uint32_t, &cat_a, k->columns, 1, &y) && y == b));
}

#define CATALOG_DB "dbname=explorer_catalog"

/* The plan's fixtures, in a database of their own so no other scenario or
 * scene sees them. */
static void catalog_fixtures_create(void) {
    connect_as(&L, 70, roomy, "");
    run(&L, "CREATE DATABASE explorer_catalog");
    assert(L.ui.q == Q_COMMAND_TAG);
    close_link(&L);
    connect_as(&L, 71, roomy, CATALOG_DB);
    run(&L, "CREATE TABLE public.no_key (a int, b text);"
            "CREATE TABLE public.base (id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,"
            " price numeric(10,2) NOT NULL CHECK (price > 0), qty int DEFAULT 1,"
            " total numeric GENERATED ALWAYS AS (price * qty) STORED);"
            "CREATE TABLE public.child (id int GENERATED BY DEFAULT AS IDENTITY,"
            " base_id bigint REFERENCES public.base (id));"
            "CREATE VIEW public.v_base AS SELECT id, price FROM public.base;"
            "CREATE MATERIALIZED VIEW public.mv_base AS SELECT id FROM public.base;"
            "CREATE TABLE public.events (at date NOT NULL, note text) PARTITION BY RANGE (at);"
            "CREATE TABLE public.events_2026 PARTITION OF public.events"
            " FOR VALUES FROM ('2026-01-01') TO ('2027-01-01');"
            "CREATE SCHEMA \"My Schema\";"
            "CREATE TABLE \"My Schema\".\"select\" (\"from\" int PRIMARY KEY, \"a\"\"b\" text);"
            "INSERT INTO \"My Schema\".\"select\" VALUES (1, 'one');"
            "CREATE FUNCTION public.add(a integer, b integer) RETURNS integer"
            " LANGUAGE sql AS 'SELECT a + b'");
    assert(L.ui.q == Q_COMMAND_TAG);
    close_link(&L);
}

/* Each fixture reads back through the metadata link with the right kind,
 * columns and constraint definitions, and partitions stay hidden. */
static void catalog_fixtures(void) {
    catalog_fixtures_create();
    meta_start(CATALOG_DB);
    catalog_session ses = M.cat.session;
    assert(ses.loaded && ses.version_num >= 150000 && ses.standard_strings);
    assert(is(ses.database, "explorer_catalog") && is(ses.user, "explorer"));
    size_t pub = schema_named("public"), my = schema_named("My Schema");
    assert(catalog_schema_count(&M.cat) == 2);
    assert(catalog_expand(&M.cat, pub) && catalog_expand(&M.cat, my));
    META_WAIT(schema_done(pub) && schema_done(my), 10000);
    assert(rel_named(pub, "no_key").kind == CATALOG_REL_TABLE);
    assert(rel_named(pub, "base").kind == CATALOG_REL_TABLE);
    assert(rel_named(pub, "v_base").kind == CATALOG_REL_VIEW);
    assert(rel_named(pub, "mv_base").kind == CATALOG_REL_MATVIEW);
    assert(rel_named(pub, "events").kind == CATALOG_REL_PARTITIONED);
    assert(!has_rel(pub, "events_2026"));
    assert(!rel_named(pub, "no_key").estimate_known); /* never analysed: reltuples -1 */
    catalog_schema ps = schema_at(pub);
    catalog_function f;
    assert(catalog_function_at(&M.cat, &ps, 0, &f) && is(f.name, "add") && f.kind == 'f');
    assert(is(f.args, "a integer, b integer") && is(f.result, "integer"));
    assert(!catalog_function_at(&M.cat, &ps, 1, &f));
    uint32_t def_oid = 0;
    bool def_ok = false;
    sc_str def;
    assert(catalog_want_definition(&M.cat, f.oid));
    META_WAIT(catalog_take_definition(&M.cat, &def_oid, &def_ok, &def), 10000);
    assert(def_oid == f.oid && def_ok);
    assert(is(def, "CREATE OR REPLACE FUNCTION public.add(a integer, b integer)\n"
                   " RETURNS integer\n"
                   " LANGUAGE sql\n"
                   "AS $function$SELECT a + b$function$\n"));

    catalog_relation r = detail_of(pub, "no_key");
    assert(is(column_at(&r, 0).name, "a") && column_at(&r, 0).key_pos == 0);
    assert(is(column_at(&r, 1).type, "text") && !column_at(&r, 1).not_null);
    catalog_constraint k;
    catalog_index ix;
    assert(!catalog_constraint_at(&M.cat, &r, 0, &k) && !catalog_index_at(&M.cat, &r, 0, &ix));

    r = detail_of(pub, "base");
    catalog_column c = column_at(&r, 0);
    assert(is(c.name, "id") && is(c.type, "bigint") && c.not_null && c.key_pos == 1);
    assert(c.identity == CATALOG_IDENTITY_ALWAYS && !c.generated && !c.has_default);
    c = column_at(&r, 1);
    assert(is(c.type, "numeric(10,2)") && c.not_null && c.identity == CATALOG_IDENTITY_NONE);
    c = column_at(&r, 2);
    assert(is(c.name, "qty") && c.has_default && is(c.default_expr, "1") && !c.generated);
    c = column_at(&r, 3);
    assert(is(c.name, "total") && c.generated && c.has_default);
    assert(is(c.default_expr, "(price * (qty)::numeric)"));
    assert(!catalog_column_at(&M.cat, &r, 4, &c));
    k = constraint_at(&r, 0);
    assert(is(k.name, "base_pkey") && k.type == 'p' && is(k.def, "PRIMARY KEY (id)"));
    assert(attnums_are(&k, 1, 0) && k.target.len == 0);
    k = constraint_at(&r, 1);
    assert(is(k.name, "base_price_check") && k.type == 'c' && is(k.def, "CHECK (price > 0::numeric)"));
    assert(attnums_are(&k, 2, 0));
    assert(!catalog_constraint_at(&M.cat, &r, 2, &k));
    assert(catalog_index_at(&M.cat, &r, 0, &ix) && ix.primary && ix.unique);
    assert(is(ix.def, "CREATE UNIQUE INDEX base_pkey ON base USING btree (id)"));

    r = detail_of(pub, "child");
    assert(column_at(&r, 0).identity == CATALOG_IDENTITY_BY_DEFAULT);
    k = constraint_at(&r, 0);
    assert(k.type == 'f' && is(k.def, "FOREIGN KEY (base_id) REFERENCES base(id)"));
    assert(is(k.target, "base") && attnums_are(&k, 2, 0));

    r = detail_of(pub, "v_base");
    assert(is(column_at(&r, 0).name, "id") && is(column_at(&r, 1).type, "numeric(10,2)"));
    r = detail_of(pub, "mv_base");
    assert(is(column_at(&r, 0).name, "id"));
    r = detail_of(pub, "events");
    assert(is(column_at(&r, 0).type, "date") && column_at(&r, 0).not_null);

    r = detail_of(my, "select");
    assert(is(column_at(&r, 0).name, "from") && column_at(&r, 0).key_pos == 1);
    assert(is(column_at(&r, 1).name, "a\"b"));
    k = constraint_at(&r, 0);
    assert(is(k.def, "PRIMARY KEY (\"from\")"));
    /* The quoted names select the table on the query link. */
    sc_str qs, qt;
    catalog_schema mys = schema_at(my);
    alignas(8) static uint8_t q_mem[512];
    sc_arena qa;
    sc_arena_init(&qa, q_mem, sizeof q_mem);
    assert(catalog_quote_ident(&cat_a, mys.name, &qa, &qs) &&
           catalog_quote_ident(&cat_a, r.name, &qa, &qt));
    char sql[256];
    snprintf(sql, sizeof sql, "SELECT \"a\"\"b\" FROM %.*s.%.*s", (int)qs.len, (const char *)qs.ptr,
             (int)qt.len, (const char *)qt.ptr);
    conn_link *q = links_cur(&LS);
    assert(link_run(q, &scratch, str(sql)) && !q->refused);
    CUR_WAIT(!active(q), 5000);
    assert(q->ui.q == Q_ROWS && q->ui.rows == 1);
    assert(links_close(&LS, 2000, wait_bell, &uiw));
    puts("ok the catalog reads every fixture back on the metadata link");
}

/* Terminating the metadata backend while the query link streams leaves the
 * stream running; the sidebar shows the loss with Retry, and Retry opens a
 * new metadata link that reads the catalog again. */
static void meta_lost_while_streaming(void) {
    meta_start("");
    size_t pub = schema_named("public");
    assert(catalog_expand(&M.cat, pub));
    META_WAIT(schema_done(pub), 10000);
    conn_link *q = links_cur(&LS);
    uint32_t first = links_meta(&LS)->ui.conn;
    assert(link_run(q, &scratch,
                    str("SELECT g, md5(g::text) || left(pg_sleep(0.001)::text, 0) AS h "
                        "FROM generate_series(1, 3000) AS g")) &&
           !q->refused);
    META_WAIT(q->ui.rows >= 200, 10000);
    connect_as(&L2, 72, roomy, "");
    run(&L2, "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
             "WHERE application_name = 'explorer-meta'");
    assert(L2.ui.q == Q_ROWS && L2.ui.rows == 1);
    close_link(&L2);
    META_WAIT(meta_shown(&M) == CATALOG_SHOW_ERROR, 5000);
    const sc_arena *ea = NULL;
    sc_str msg;
    assert(meta_error_of(&M, &ea, &msg) == META_ERROR_LOST && meta_can_retry(&M));
    assert(sidebar_says("The metadata connection was lost.", true));
    assert(active(q) && !q->broken && q->ui.rows < 3000); /* still streaming */
    assert(schema_done(pub)); /* the tree stays */
    META_WAIT(!active(q), 20000);
    assert(q->ui.q == Q_ROWS && q->ui.rows == 3000);
    assert(meta_retry(&M));
    META_WAIT(meta_shown(&M) == CATALOG_SHOW_READY && schema_done(pub), 10000);
    assert(links_meta(&LS)->ui.conn != first && schema_at(pub).expanded);
    assert(links_close(&LS, 2000, wait_bell, &uiw));
    puts("ok a lost metadata link leaves the stream running; Retry reads the catalog again");
}

/* A catalog read behind another session's ACCESS EXCLUSIVE lock ends with
 * the 10 s statement_timeout's message, within 11 s, even when the user's
 * server options set statement_timeout=0; once the lock goes the same read
 * succeeds. */
static void lock_timeout_on(const char *info) {
    meta_start(info);
    size_t pub = schema_named("public");
    assert(catalog_expand(&M.cat, pub));
    META_WAIT(schema_done(pub), 10000);
    connect_as(&L2, 73, roomy, CATALOG_DB);
    run(&L2, "BEGIN; LOCK TABLE public.base IN ACCESS EXCLUSIVE MODE");
    assert(L2.ui.q == Q_COMMAND_TAG);
    uint64_t t0 = sc_clock_ms();
    catalog_relation r = rel_named(pub, "base");
    assert(catalog_want_detail(&M.cat, schema_at(pub).oid, r.oid));
    META_WAIT(rel_named(pub, "base").detail == CATALOG_LOAD_FAILED, 12000);
    uint64_t took = sc_clock_ms() - t0;
    r = rel_named(pub, "base");
    assert(took < 11000 && took >= 9000);
    assert(is(r.error, "canceling statement due to statement timeout"));
    run(&L2, "ROLLBACK");
    close_link(&L2);
    META_WAIT(rel_named(pub, "base").detail_left == 0, 12000);
    r = detail_of(pub, "base");
    assert(is(column_at(&r, 0).name, "id"));
    assert(links_close(&LS, 2000, wait_bell, &uiw));
    printf("ok a catalog read behind a lock times out in %llu ms (%s)\n", (unsigned long long)took,
           info);
}

static void catalog_lock_timeout(void) {
    lock_timeout_on(CATALOG_DB " options='-c statement_timeout=0'");
    assert(setenv("PGOPTIONS", "-c statement_timeout=0", 1) == 0);
    lock_timeout_on(CATALOG_DB);
    assert(unsetenv("PGOPTIONS") == 0);
}

typedef struct {
    char a[64], b[64];
} two_cells;

static void first_row(void *ctx, uint64_t row, uint64_t col, proto_cell c) {
    two_cells *t = ctx;
    if (row == 0 && col < 2) {
        copy_str(col == 0 ? t->a : t->b, sizeof t->a, c.bytes);
    }
}

/* search_path and statement_timeout on the metadata link, read once the
 * catalog is idle; the result goes back to the pool as meta.c's do. */
static two_cells meta_settings(void) {
    conn_link *m = links_meta(&LS);
    assert(m != NULL && !M.running);
    run(m, "SELECT pg_catalog.current_setting('search_path'), "
           "pg_catalog.current_setting('statement_timeout')");
    assert(m->ui.q == Q_ROWS && m->ui.rows == 1);
    two_cells t;
    memset(&t, 0, sizeof t);
    (void)walk(m, first_row, &t);
    conn_ui shown = m->ui;
    result kept;
    assert(link_keep(m, &kept));
    (void)link_drop_result(m, &kept, &shown);
    return t;
}

static void meta_settings_are(const char *info, const char *path) {
    meta_start(info);
    two_cells t = meta_settings();
    if (strcmp(t.a, path) != 0 || strcmp(t.b, "10s") != 0) {
        fprintf(stderr, "metadata link on '%s': search_path %s, statement_timeout %s\n", info, t.a, t.b);
        abort();
    }
    assert(links_close(&LS, 2000, wait_bell, &uiw));
}

/* The metadata link takes the user's server options from wherever libpq
 * finds them, as the query link does: the conninfo's options, PGOPTIONS, or
 * a pg_service.conf entry. Its own 10 s statement_timeout still wins over
 * one the user set. */
static void meta_keeps_user_options(void) {
    meta_settings_are("options='-c search_path=fromconninfo -c statement_timeout=0'", "fromconninfo");
    assert(setenv("PGOPTIONS", "-c search_path=fromenv -c statement_timeout=0", 1) == 0);
    meta_settings_are("", "fromenv");
    assert(unsetenv("PGOPTIONS") == 0);
    char path[64];
    snprintf(path, sizeof path, "/tmp/explorer-pq-%ld.conf", (long)getpid());
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fputs("[explorer_svc]\noptions=-c search_path=fromservice -c statement_timeout=0\n", f);
    assert(fclose(f) == 0);
    assert(setenv("PGSERVICEFILE", path, 1) == 0);
    meta_settings_are("service=explorer_svc", "fromservice");
    assert(unsetenv("PGSERVICEFILE") == 0 && remove(path) == 0);
    puts("ok the metadata link keeps conninfo, PGOPTIONS and service-file options, with its 10 s timeout");
}

/* The bootstrap behind a lock on pg_namespace: the timeout's SET runs, the
 * schema list read times out and the sidebar shows the catalog's error. Once
 * the lock goes, its Retry, which needs no retiring slot since the metadata
 * link is up, is offered with both slots busy and reads the catalog. */
static void catalog_retry_after_failed_bootstrap(void) {
    connect_as(&L2, 75, roomy, "");
    run(&L2, "BEGIN; LOCK TABLE pg_catalog.pg_namespace IN ACCESS EXCLUSIVE MODE");
    assert(L2.ui.q == Q_COMMAND_TAG);
    links_init(&LS, &P, roomy, ring, &uiw, &info_a, set_info(""));
    sc_arena_init(&cat_a, cat_mem, sizeof cat_mem);
    sc_arena_init(&keep_a, keep_mem, sizeof keep_mem);
    sc_arena_init(&msql_a, msql_mem, sizeof msql_mem);
    assert(meta_init(&M, &LS, &cat_a, &keep_a, &msql_a));
    uint64_t t0 = sc_clock_ms();
    assert(links_open(&LS));
    META_WAIT(meta_shown(&M) == CATALOG_SHOW_ERROR, 15000);
    uint64_t took = sc_clock_ms() - t0;
    assert(took < 11000);
    const sc_arena *ea = NULL;
    sc_str msg;
    assert(meta_error_of(&M, &ea, &msg) == META_ERROR_CATALOG);
    assert(sc_str_eq_lit(ea, msg, (const uint8_t *)"canceling statement due to statement timeout", 44));
    assert(meta_link(&M) == CATALOG_LINK_UP);
    assert(sidebar_says("canceling statement due to statement timeout", true));
    run(&L2, "ROLLBACK");
    close_link(&L2);
    const char *hole = "host=10.255.255.1 connect_timeout=30";
    result kept;
    assert(link_init(&R, &P, BUDGET_QUERY, 76, roomy, ring, &uiw) && link_open(&R, &scratch, str(hole)));
    assert(link_retire(&R, &kept));
    assert(link_init(&R2, &P, BUDGET_QUERY, 77, roomy, ring, &uiw) && link_open(&R2, &scratch, str(hole)));
    assert(link_retire(&R2, &kept));
    assert(!conn_pool_can_retire(&P.book) && meta_can_retry(&M));
    assert(sidebar_says("canceling statement due to statement timeout", true));
    assert(meta_shown(&M) == CATALOG_SHOW_ERROR && meta_retry(&M));
    META_WAIT(meta_shown(&M) == CATALOG_SHOW_READY && M.cat.session.loaded, 10000);
    retired_ends(&R, 2000);
    retired_ends(&R2, 2000);
    assert(links_close(&LS, 2000, wait_bell, &uiw));
    printf("ok a failed bootstrap read shows the catalog error in %llu ms; Retry reads it again\n",
           (unsigned long long)took);
}

/* Reconnect retires the query link and, while its worker runs, the metadata
 * link: with one retiring slot already busy it is refused until that slot
 * frees. */
static void meta_link_takes_retiring_slot(void) {
    meta_start("");
    const char *hole = "host=10.255.255.1 connect_timeout=30";
    result kept;
    assert(link_init(&R, &P, BUDGET_QUERY, 78, roomy, ring, &uiw) && link_open(&R, &scratch, str(hole)));
    assert(link_retire(&R, &kept));
    assert(P.book.retiring == BUDGET_RETIRING_SLOTS - 1u);
    assert(!links_can_retire(&LS));
    retired_ends(&R, 2000);
    assert(links_can_retire(&LS));
    assert(links_close(&LS, 2000, wait_bell, &uiw));
    puts("ok with the metadata link up, Reconnect needs two retiring slots");
}

/* The metadata link fails to connect while the query link is up (a role
 * allowed one connection): the sidebar shows libpq's message with Retry, and
 * queries carry on. */
static void meta_connect_fails(void) {
    connect_as(&L2, 79, roomy, "");
    run(&L2, "CREATE ROLE explorer_one LOGIN PASSWORD 'explorer' CONNECTION LIMIT 1");
    assert(L2.ui.q == Q_COMMAND_TAG);
    close_link(&L2);
    links_init(&LS, &P, roomy, ring, &uiw, &info_a, set_info("user=explorer_one password=explorer"));
    sc_arena_init(&cat_a, cat_mem, sizeof cat_mem);
    sc_arena_init(&keep_a, keep_mem, sizeof keep_mem);
    sc_arena_init(&msql_a, msql_mem, sizeof msql_mem);
    assert(meta_init(&M, &LS, &cat_a, &keep_a, &msql_a));
    assert(links_open(&LS));
    META_WAIT(meta_shown(&M) == CATALOG_SHOW_ERROR, 10000);
    const sc_arena *ea = NULL;
    sc_str msg;
    char text[256];
    assert(meta_error_of(&M, &ea, &msg) == META_ERROR_CONNECT);
    copy_str(text, sizeof text, msg);
    assert(strstr(text, "too many connections") != NULL);
    alignas(8) static uint8_t fa_mem[1024];
    sc_arena fa;
    sc_arena_init(&fa, fa_mem, sizeof fa_mem);
    bool retry = false;
    assert(meta_sidebar_error(&M, &fa, &ea, &msg, &retry) && retry);
    copy_str(text, sizeof text, msg);
    assert(strstr(text, "too many connections") != NULL);
    conn_link *q = links_cur(&LS);
    assert(link_run(q, &scratch, str("SELECT 1")) && !q->refused);
    CUR_WAIT(!active(q), 5000);
    assert(q->ui.q == Q_ROWS && q->ui.rows == 1);
    assert(links_close(&LS, 2000, wait_bell, &uiw));
    puts("ok a metadata link that cannot connect shows libpq's message; queries carry on");
}

/* The query link lost: the sidebar keeps its tree and reads Disconnected,
 * with no Retry of its own. Reconnect rebuilds both links, and the catalog
 * reads again with its expansion kept by name. */
static void reconnect_rebuilds_meta(void) {
    meta_start("");
    size_t pub = schema_named("public");
    assert(catalog_expand(&M.cat, pub));
    META_WAIT(schema_done(pub), 10000);
    uint32_t first = links_meta(&LS)->ui.conn;
    connect_as(&L2, 74, roomy, "");
    run(&L2, "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
             "WHERE application_name = 'explorer'");
    close_link(&L2);
    META_WAIT(meta_query_link(&LS) == CATALOG_LINK_FAILED, 5000);
    assert(meta_shown(&M) == CATALOG_SHOW_DISCONNECTED && !meta_retry(&M));
    assert(catalog_schema_count(&M.cat) > 0 && schema_done(pub));
    link_handoff h;
    bool handed = false;
    assert(links_reconnect(&LS, &h, &handed) && handed);
    assert(links_meta(&LS) == NULL);
    META_WAIT(meta_shown(&M) == CATALOG_SHOW_READY && schema_done(schema_named("public")), 10000);
    assert(links_meta(&LS)->ui.conn != first && schema_at(schema_named("public")).expanded);
    assert(links_close(&LS, 2000, wait_bell, &uiw));
    puts("ok Reconnect rebuilds the metadata link and keeps the expansion");
}

/* Runs one scenario; afterwards every arena is back in the pool. */
static void scenario(void (*f)(void)) {
    f();
    assert(link_pool_used(&P) == 0);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); /* keep the progress lines when a scenario aborts */
    sc_arena_init(&scratch, scratch_mem, sizeof scratch_mem);
    assert(sc_wake_new(&uiw).ok);
    assert(link_pool_init(&P, budget_default_split()));
    scenario(wide_window);
    scenario(stream_25000);
    scenario(commands_and_copy);
    scenario(error_after_rows);
    scenario(null_empty_truncated);
    scenario(unsent_busy_and_flush);
    scenario(cancel);
    scenario(multi_statement);
    scenario(connect_failures);
    scenario(terminate_and_close);
    scenario(fast_stream_close);
    scenario(oversized_replies);
    scenario(idle_loss);
    scenario(connect_timeout_and_refused_open);
    scenario(tabs_keep_and_close);
    scenario(quit_while_streaming);
    scenario(million_rows);
    scenario(kept_result_outlives_link);
    scenario(disconnect_then_reconnect);
    scenario(lost_then_reconnect);
    scenario(cancel_while_connecting);
    scenario(retiring_slots);
    scenario(free_without_bye);
    scenario(broken_link_keeps_rows);
    scenario(retired_link_breaks);
    scenario(link_set_retiring_slots);
    scenario(link_set_reconnect_after_lost);
    scenario(link_set_close);
    scenario(catalog_fixtures);
    scenario(meta_lost_while_streaming);
    scenario(catalog_lock_timeout);
    scenario(meta_keeps_user_options);
    scenario(catalog_retry_after_failed_bootstrap);
    scenario(meta_link_takes_retiring_slot);
    scenario(meta_connect_fails);
    scenario(reconnect_rebuilds_meta);
    scenario(keepalives_reach_libpq);
    scenario(late_cancel_spares_next_run);
#ifdef LIBPQ_HAS_ASYNC_CANCEL
    scenario(cancel_never_answered);
#endif
    link_pool_free(&P);
    sc_wake_free(&uiw);
    puts("explorer_pq_integration: ok");
    return 0;
}
