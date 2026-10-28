/* Tests the query view's words (examples/explorer/query_text.c): display
 * copies, the terminal reply reader, and the toolbar, bottom bar and notice
 * texts for every state. Core code, built in. */
#include "../examples/explorer/budget.c"
#include "../examples/explorer/conn.c"
#include "../examples/explorer/frame.c"
#include "../examples/explorer/proto.c"
#include "../examples/explorer/query_text.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static alignas(8) uint8_t text_mem[1u << 16];
static alignas(8) uint8_t msg_mem[1u << 14];
static sc_arena ta, ma;
static bool sunk; /* the tests' allocation-failure sink; no allocation here fails */

static bool is(sc_str s, const char *want) {
    return s.len == strlen(want) && (s.len == 0 || memcmp(s.ptr, want, s.len) == 0);
}

static sc_str lit(sc_arena *a, const char *s) {
    return sc_str_from_lit(a, (const uint8_t *)s, strlen(s));
}

static void display_copies(void) {
    assert(sc_arena_reset(&ta));
    sc_str raw = lit(&ta, "a\tb\nc\x7f" "d\x1f" "caf\xc3\xa9");
    assert(is(text_clean_copy(&ta, &ta, raw, 64, &sunk), "a b c d caf\xc3\xa9"));
    assert(is(text_clean_copy(&ta, &ta, raw, 3, &sunk), "a b\xE2\x80\xA6"));
    sc_str empty = {NULL, 0, ta.epoch};
    assert(text_clean_copy(&ta, &ta, empty, 8, &sunk).len == 0);
    /* A cut inside a UTF-8 sequence moves back to its first byte, and any
     * cut by the cap ends with an ellipsis. */
    sc_str accents = lit(&ta, "ab\xc3\xa9\xe2\x82\xac"); /* a b é € */
    assert(is(text_clean_copy(&ta, &ta, accents, 3, &sunk), "ab\xE2\x80\xA6"));
    assert(is(text_clean_copy(&ta, &ta, accents, 4, &sunk), "ab\xc3\xa9\xE2\x80\xA6"));
    assert(is(text_clean_copy(&ta, &ta, accents, 6, &sunk), "ab\xc3\xa9\xE2\x80\xA6"));
    assert(is(text_clean_copy(&ta, &ta, accents, 7, &sunk), "ab\xc3\xa9\xe2\x82\xac"));
    assert(is(text_cut(&ta, &ta, accents, 5, &sunk), "ab\xc3\xa9\xE2\x80\xA6"));
    assert(is(text_cut(&ta, &ta, accents, 1, &sunk), "a\xE2\x80\xA6"));
    assert(is(text_cut(&ta, &ta, accents, 0, &sunk), "\xE2\x80\xA6"));
    /* Each invalid byte shows as U+FFFD; a C1 control as a space. The cap
     * counts output bytes, so U+FFFD's three never come in part. */
    sc_str bad = lit(&ta, "x\xff\xc3" "y\xc2\x85z");
    assert(is(text_clean_copy(&ta, &ta, bad, 64, &sunk),
              "x\xEF\xBF\xBD\xEF\xBF\xBDy z"));
    assert(is(text_clean_copy(&ta, &ta, bad, 6, &sunk), "x\xEF\xBF\xBD\xE2\x80\xA6"));
    assert(is(text_clean_copy(&ta, &ta, bad, 7, &sunk), "x\xEF\xBF\xBD\xEF\xBF\xBD\xE2\x80\xA6"));
    assert(is(text_cut(&ta, &ta, bad, 2, &sunk), "x\xEF\xBF\xBD\xE2\x80\xA6"));
    /* U+FFFD triples invalid bytes, so text past the cap must not vanish
     * silently: 40 bad bytes and "tail" at a 96-byte cap. */
    uint8_t latin[44];
    memset(latin, 0xe9, 40);
    memcpy(latin + 40, "tail", 4);
    sc_str many = text_clean_copy(&ta, &ta, sc_str_from_lit(&ta, latin, sizeof latin), 96, &sunk);
    assert(many.len == 99 && is(sc_slice_sub(&ta, many, 96, 3), "\xE2\x80\xA6"));
    /* Two combining marks stay on their character; a third is dropped, and
     * the count starts again at the next character. */
    sc_str stack = lit(&ta, "e\xcc\x81\xcc\x81\xcc\x81" "a\xcc\x81\xcc\x81");
    assert(is(text_clean_copy(&ta, &ta, stack, 64, &sunk), "e\xcc\x81\xcc\x81" "a\xcc\x81\xcc\x81"));
    /* Cells: newline and tab as arrows on one line; a truncated value ends
     * with an ellipsis even when its stored bytes fit. */
    sc_str nl = lit(&ta, "a\nb\tc\x01");
    assert(is(text_cell_copy(&ta, &ta, nl, 64, false, &sunk), "a\xE2\x86\xB5" "b\xE2\x86\x92" "c "));
    assert(is(text_cell_copy(&ta, &ta, nl, 64, true, &sunk),
              "a\xE2\x86\xB5" "b\xE2\x86\x92" "c \xE2\x80\xA6"));
    assert(is(text_cell_copy(&ta, &ta, nl, 3, false, &sunk), "a\xE2\x80\xA6"));
    /* An incomplete last character goes; a complete one or a stray byte stays. */
    sc_str cutoff = lit(&ta, "ab\xe2\x82");
    assert(is(text_whole_chars(&ta, cutoff), "ab"));
    assert(is(text_whole_chars(&ta, lit(&ta, "e\xcc")), "e"));
    assert(is(text_whole_chars(&ta, lit(&ta, "\xf0\x9f\x98")), ""));
    assert(is(text_whole_chars(&ta, lit(&ta, "ab\xe2\x82\xac")), "ab\xe2\x82\xac"));
    assert(is(text_whole_chars(&ta, lit(&ta, "ab\x80")), "ab\x80"));
    assert(is(text_whole_chars(&ta, lit(&ta, "ab\xff")), "ab\xff"));
    /* Cut by the cap and truncated too: one ellipsis, not two. */
    assert(is(text_cell_copy(&ta, &ta, lit(&ta, "abcdefghij"), 4, true, &sunk), "abcd\xE2\x80\xA6"));
    assert(is(text_clean_copy(&ta, &ta, nl, 64, &sunk), "a b c "));
    /* U+00A0 is just past the C1 range: copied as it is. */
    assert(is(text_clean_copy(&ta, &ta, lit(&ta, "\xc2\xa0"), 8, &sunk), "\xc2\xa0"));
    /* A stale source fails the whole line, not just its own part. */
    assert(sc_arena_reset(&ma));
    sc_str gone = lit(&ma, "gone");
    assert(sc_arena_reset(&ma));
    text_line sl = text_new(&ta, 16, &sunk);
    TEXT_LIT(&sl, "x");
    text_clean(&sl, &ma, gone, 8);
    assert(sl.failed && text_view(&sl).len == 0);
    /* A copy that does not fit fails whole, not in part. */
    text_line l = text_new(&ta, 4, &sunk);
    TEXT_LIT(&l, "abcde");
    assert(l.failed && text_view(&l).len == 0);
    assert(!sunk); /* only a failed allocation reports to the sink */
}

/* Plan U16: when the frame arena runs out while a label is copied, the frame
 * builder's sticky flag is set, so the frame is refused rather than drawn
 * with the label missing. */
static void failed_allocation_fails_the_frame(void) {
    alignas(8) static uint8_t small[8192];
    sc_arena fa;
    sc_arena_init(&fa, small, sizeof small);
    frame_builder b;
    ui_frame f;
    assert(frame_begin(&b, &fa, 4, 4));
    assert(frame_open(&b, frame_box()));
    text_line ok = text_new(&fa, 16, &b.failed);
    TEXT_LIT(&ok, "fits");
    assert(!b.failed && frame_text(&b, text_view(&ok), UI_FONT_SANS, 13, (ui_color){0, 0, 0, 255}));
    while (sc_arena_alloc(&fa, 8).len == 8) { /* fill the rest */
    }
    text_line l = text_new(&fa, 64, &b.failed);
    TEXT_LIT(&l, "Query Result");
    assert(l.failed && b.failed);
    assert(!frame_text(&b, text_view(&l), UI_FONT_SANS, 13, (ui_color){0, 0, 0, 255}));
    assert(!frame_close(&b) && !frame_end(&b, &f));
    /* The copy helpers report the same way. */
    bool lost = false;
    assert(text_clean_copy(&fa, &ta, lit(&ta, "label"), 16, &lost).len == 0 && lost);
    lost = false;
    assert(text_cut(&fa, &ta, lit(&ta, "label"), 3, &lost).len == 0 && lost);
    lost = false;
    assert(text_cell_copy(&fa, &ta, lit(&ta, "cell"), 16, false, &lost).len == 0 && lost);
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
    return query_count(&ta, u, &ma, &t, ms, status, &sunk);
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
    u.offer_disconnect = true; /* 5 s in: still cancelling */
    assert(is(count(&u, Q_CANCELLING, 5, 0, &st), "Cancelling\xE2\x80\xA6"));
    u.cancel_failed = true;
    assert(is(count(&u, Q_CANCELLING, 5, 0, &st), "Cancel failed") && st == SHELL_STATUS_ERROR);
    u.offer_disconnect = false;
    u.cancel_failed = false;
    assert(is(count(&u, Q_ROWS, 1, 3, &st), "1 row") && st == SHELL_STATUS_OK);
    assert(is(count(&u, Q_ROWS, 0, 3, &st), "0 rows") && st == SHELL_STATUS_OK);
    assert(is(count(&u, Q_COMMAND_TAG, 0, 3, &st), "INSERT 0 3") && st == SHELL_STATUS_OK);
    assert(is(count(&u, Q_ERROR, 0, 3, &st), "Error") && st == SHELL_STATUS_ERROR);
    assert(is(count(&u, Q_ERROR_AFTER_ROWS, 59, 3, &st), "59 rows, then an error") &&
           st == SHELL_STATUS_ERROR);
    u.cap = PROTO_CAP_LIMIT;
    assert(is(count(&u, Q_CAPPED, 10000, 3, &st), "10,000 rows (limit)") &&
           st == SHELL_STATUS_ERROR);
    u.cap = PROTO_CAP_QUOTA;
    assert(is(count(&u, Q_CAPPED, 2, 3, &st), "2 rows (memory limit)"));
    assert(is(count(&u, Q_CANCELLED, 1, 3, &st), "Cancelled (1 row)") &&
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
              "That does not fit: the editor holds 1 MiB."));
    assert(is(query_notice(&ta, QUERY_NOTICE_CLIP_TOO_BIG),
              "The clipboard holds more than 1 MiB, so nothing was pasted."));
    assert(is(query_notice(&ta, QUERY_NOTICE_NOT_TEXT),
              "Only UTF-8 text without control characters can be typed or pasted."));
    assert(is(query_notice(&ta, QUERY_NOTICE_STALE),
              "That text is no longer valid, so nothing was inserted."));
    assert(query_insert_notice(EDITOR_OK) == QUERY_NOTICE_NONE);
    assert(query_insert_notice(EDITOR_TOO_BIG) == QUERY_NOTICE_TOO_BIG);
    assert(query_insert_notice(EDITOR_NOT_TEXT) == QUERY_NOTICE_NOT_TEXT);
    assert(query_insert_notice(EDITOR_STALE) == QUERY_NOTICE_STALE);
    assert(query_notice(&ta, QUERY_NOTICE_BUSY).len > 0);
    assert(query_notice(&ta, QUERY_NOTICE_NOT_CONNECTED).len > 0);
    assert(query_notice(&ta, QUERY_NOTICE_POOL).len > 0);
    assert(is(query_notice(&ta, QUERY_NOTICE_TABS_FULL), "All nine tabs are open. Close one to open another."));
    assert(is(query_notice(&ta, QUERY_NOTICE_RUN_TOO_BIG), "A run sends at most 127 KiB. Select part of the text and run that."));
}

/* The query toolbar's stop button and the reconnect offer (plan U13). */
static void stop_and_reconnect(void) {
    conn_ui u;
    memset(&u, 0, sizeof u);
    assert(query_stop_for(&u, false, false) == QUERY_STOP_DISABLED); /* idle */
    assert(query_stop_for(&u, false, true) == QUERY_STOP_CANCEL);    /* queued behind another tab */
    u.q = Q_ROWS;
    assert(query_stop_for(&u, true, false) == QUERY_STOP_DISABLED);  /* finished */
    for (int q = Q_QUEUED; q <= Q_CANCELLING; q++) {
        u.q = (query_state)q;
        assert(query_stop_for(&u, true, false) == QUERY_STOP_CANCEL);
        assert(query_stop_for(&u, false, false) == QUERY_STOP_DISABLED); /* another tab's copy */
    }
    u.offer_disconnect = true; /* 5 s in Cancelling, or the cancel failed */
    assert(query_stop_for(&u, true, false) == QUERY_STOP_DISCONNECT);
    for (int st = UI_CONN_NONE; st <= UI_CONN_CLOSED; st++) {
        bool want = st == UI_CONN_AUTH_FAILED || st == UI_CONN_UNREACHABLE ||
                    st == UI_CONN_DISCONNECTED;
        assert(query_reconnect_offered((ui_conn_state)st, false) == want);
        assert(query_reconnect_offered((ui_conn_state)st, true)); /* a broken link */
    }
    assert(sc_arena_reset(&ta));
    assert(is(query_reconnect_label(&ta, true), "Reconnect"));
    assert(is(query_reconnect_label(&ta, false), "Waiting for the old connection"));
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
    failed_allocation_fails_the_frame();
    terminal_reader();
    bottom_bar();
    connection_labels();
    notices();
    active_states();
    stop_and_reconnect();
    assert(!sunk);
    puts("explorer_query_text_test: ok");
    return 0;
}
