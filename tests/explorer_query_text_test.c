/* Tests the query view's words (examples/explorer/query_text.c): display
 * copies, the terminal reply reader, and the toolbar, bottom bar and notice
 * texts for every state. Core code, built in. */
#include "../examples/explorer/conn.c"
#include "../examples/explorer/proto.c"
#include "../examples/explorer/query_text.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static alignas(8) uint8_t text_mem[1u << 16];
static alignas(8) uint8_t msg_mem[1u << 14];
static sc_arena ta, ma;

static bool is(sc_str s, const char *want) {
    return s.len == strlen(want) && (s.len == 0 || memcmp(s.ptr, want, s.len) == 0);
}

static sc_str lit(sc_arena *a, const char *s) {
    return sc_str_from_lit(a, (const uint8_t *)s, strlen(s));
}

static void display_copies(void) {
    assert(sc_arena_reset(&ta));
    sc_str raw = lit(&ta, "a\tb\nc\x7f" "d\x1f" "caf\xc3\xa9");
    assert(is(text_clean_copy(&ta, &ta, raw, 64), "a b c d caf\xc3\xa9"));
    assert(is(text_clean_copy(&ta, &ta, raw, 3), "a b"));
    sc_str empty = {NULL, 0, ta.epoch};
    assert(text_clean_copy(&ta, &ta, empty, 8).len == 0);
    /* A cut inside a UTF-8 sequence moves back to its first byte. */
    sc_str accents = lit(&ta, "ab\xc3\xa9\xe2\x82\xac"); /* a b é € */
    assert(is(text_clean_copy(&ta, &ta, accents, 3), "ab"));
    assert(is(text_clean_copy(&ta, &ta, accents, 4), "ab\xc3\xa9"));
    assert(is(text_clean_copy(&ta, &ta, accents, 6), "ab\xc3\xa9"));
    assert(is(text_clean_copy(&ta, &ta, accents, 7), "ab\xc3\xa9\xe2\x82\xac"));
    assert(is(text_cut(&ta, &ta, accents, 5), "ab\xc3\xa9\xE2\x80\xA6"));
    assert(is(text_cut(&ta, &ta, accents, 1), "a\xE2\x80\xA6"));
    assert(is(text_cut(&ta, &ta, accents, 0), "\xE2\x80\xA6"));
    /* A copy that does not fit fails whole, not in part. */
    text_line l = text_new(&ta, 4);
    TEXT_LIT(&l, "abcde");
    assert(l.failed && text_view(&l).len == 0);
}

/* A terminal reply in `ma`: two tags, a malformed item and an error, as asked. */
static void terminal_with(bool error, bool tag, bool bad) {
    proto_writer w;
    proto_header h = {error ? PROTO_ERROR : PROTO_DONE, 1, 7, 0, 0, 0};
    assert(sc_arena_reset(&ma) && proto_begin(&w, &ma, h));
    if (tag) {
        assert(proto_put_u64(&w, PROTO_ITEM_TAG) && proto_put_str(&w, &ta, lit(&ta, "SELECT 1")));
        assert(proto_put_u64(&w, PROTO_ITEM_TAG) &&
               proto_put_str(&w, &ta, lit(&ta, "INSERT 0 3")));
    }
    if (bad) {
        assert(proto_put_u64(&w, 9)); /* not an item kind */
    }
    if (error) {
        assert(proto_put_u64(&w, PROTO_ITEM_ERROR) && proto_put_str(&w, &ta, lit(&ta, "42703")) &&
               proto_put_str(&w, &ta, lit(&ta, "column \"x\" does not exist")) &&
               proto_put_str(&w, &ta, lit(&ta, "")) &&
               proto_put_str(&w, &ta, lit(&ta, "Perhaps x2")) && proto_put_u64(&w, 8));
    }
    assert(proto_put_u64(&w, PROTO_ITEM_END) && proto_put_u64(&w, 0) && proto_end(&w));
}

static void terminal_reader(void) {
    assert(sc_arena_reset(&ta));
    query_terminal t = query_terminal_read(NULL);
    assert(!t.has_tag && !t.error);
    terminal_with(true, true, false);
    t = query_terminal_read(&ma);
    assert(t.has_tag && is(t.tag, "INSERT 0 3")); /* the last tag */
    assert(t.error && is(t.sqlstate, "42703") && is(t.message, "column \"x\" does not exist"));
    assert(t.detail.len == 0 && is(t.hint, "Perhaps x2") && t.position == 8);
    terminal_with(true, false, true); /* nothing after a bad item is read */
    t = query_terminal_read(&ma);
    assert(!t.error && !t.has_tag);
}

static sc_str count(conn_ui *u, query_state q, uint64_t rows, uint64_t ms, uint8_t *status) {
    query_terminal t = query_terminal_read(&ma);
    u->q = q;
    u->rows = rows;
    return query_count(&ta, u, &ma, &t, ms, status);
}

static void bottom_bar(void) {
    conn_ui u;
    memset(&u, 0, sizeof u);
    uint8_t st = 9;
    assert(sc_arena_reset(&ta));
    terminal_with(false, true, false);
    assert(is(count(&u, Q_NONE, 0, 0, &st), "No rows") && st == SHELL_STATUS_NONE);
    assert(is(count(&u, Q_QUEUED, 0, 850, &st), "Running\xE2\x80\xA6 850 ms") &&
           st == SHELL_STATUS_NONE);
    assert(is(count(&u, Q_RUNNING, 0, 1250, &st), "Running\xE2\x80\xA6 1.3 s"));
    assert(is(count(&u, Q_STREAMING, 1234, 2000, &st), "1,234 rows\xE2\x80\xA6 2.0 s") &&
           st == SHELL_STATUS_NONE);
    assert(is(count(&u, Q_CANCELLING, 5, 0, &st), "Cancelling\xE2\x80\xA6"));
    assert(is(count(&u, Q_ROWS, 1, 3, &st), "1 row") && st == SHELL_STATUS_OK);
    assert(is(count(&u, Q_ROWS, 0, 3, &st), "0 rows") && st == SHELL_STATUS_OK);
    assert(is(count(&u, Q_COMMAND_TAG, 0, 3, &st), "INSERT 0 3") && st == SHELL_STATUS_OK);
    assert(is(count(&u, Q_ERROR, 0, 3, &st), "Error") && st == SHELL_STATUS_ERROR);
    assert(is(count(&u, Q_ERROR_AFTER_ROWS, 59, 3, &st), "59 rows, then an error") &&
           st == SHELL_STATUS_ERROR);
    u.cap = PROTO_CAP_LIMIT;
    assert(is(count(&u, Q_CAPPED, 10000, 3, &st), "First 10,000 rows, stopped at the row limit") &&
           st == SHELL_STATUS_ERROR);
    u.cap = PROTO_CAP_QUOTA;
    assert(is(count(&u, Q_CAPPED, 2, 3, &st), "First 2 rows, stopped at the memory limit"));
    assert(is(count(&u, Q_CANCELLED, 1, 3, &st), "Cancelled, 1 row kept") &&
           st == SHELL_STATUS_ERROR);
    assert(is(count(&u, Q_DISCONNECTED, 12, 3, &st), "Disconnected, 12 rows kept") &&
           st == SHELL_STATUS_ERROR);
    terminal_with(false, false, false); /* a tag state whose reply holds no tag */
    assert(is(count(&u, Q_COMMAND_TAG, 0, 3, &st), "Done"));
}

static void connection_labels(void) {
    uint8_t dot = 9;
    sc_str label;
    assert(sc_arena_reset(&ta));
    query_connection(&ta, UI_CONN_NONE, false, &dot, &label);
    assert(dot == SHELL_OFFLINE && is(label, "Not connected"));
    query_connection(&ta, UI_CONN_CONNECTING, false, &dot, &label);
    assert(dot == SHELL_CONNECTING && is(label, "Connecting\xE2\x80\xA6"));
    query_connection(&ta, UI_CONN_CONNECTED, false, &dot, &label);
    assert(dot == SHELL_CONNECTED && is(label, "Connected"));
    query_connection(&ta, UI_CONN_CONNECTED, true, &dot, &label); /* a broken link */
    assert(dot == SHELL_FAILED && is(label, "Disconnected"));
    query_connection(&ta, UI_CONN_AUTH_FAILED, false, &dot, &label);
    assert(dot == SHELL_FAILED && is(label, "Authentication failed"));
    query_connection(&ta, UI_CONN_UNREACHABLE, false, &dot, &label);
    assert(dot == SHELL_FAILED && is(label, "Can't reach server"));
    query_connection(&ta, UI_CONN_DISCONNECTED, false, &dot, &label);
    assert(dot == SHELL_FAILED && is(label, "Disconnected"));
    query_connection(&ta, UI_CONN_CLOSED, false, &dot, &label);
    assert(is(label, "Disconnected"));
}

static void notices(void) {
    assert(sc_arena_reset(&ta));
    assert(query_notice(&ta, QUERY_NOTICE_NONE).len == 0);
    assert(is(query_notice(&ta, QUERY_NOTICE_TOO_BIG),
              "That does not fit: the input box holds 64 KiB."));
    assert(is(query_notice(&ta, QUERY_NOTICE_CLIP_TOO_BIG),
              "The clipboard holds more than 64 KiB, so nothing was pasted."));
    assert(is(query_notice(&ta, QUERY_NOTICE_NOT_ASCII),
              "Only printable ASCII, tabs and newlines can be typed or pasted for now."));
    assert(query_notice(&ta, QUERY_NOTICE_BUSY).len > 0);
    assert(query_notice(&ta, QUERY_NOTICE_NOT_CONNECTED).len > 0);
    assert(query_notice(&ta, QUERY_NOTICE_POOL).len > 0);
}

static void active_states(void) {
    conn_ui u;
    memset(&u, 0, sizeof u);
    for (int q = Q_NONE; q <= Q_DISCONNECTED; q++) {
        u.q = (query_state)q;
        bool want = q == Q_QUEUED || q == Q_RUNNING || q == Q_STREAMING || q == Q_CANCELLING;
        assert(conn_ui_active(&u) == want);
    }
}

int main(void) {
    sc_arena_init(&ta, text_mem, sizeof text_mem);
    sc_arena_init(&ma, msg_mem, sizeof msg_mem);
    display_copies();
    terminal_reader();
    bottom_bar();
    connection_labels();
    notices();
    active_states();
    puts("explorer_query_text_test: ok");
    return 0;
}
