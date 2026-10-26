#include "query_text.h"

#define TERMINAL_ITEMS 100000u /* more items than a terminal arena can hold */
#define TAG_SHOWN_BYTES 96u

text_line text_new(sc_arena *a, size_t cap) {
    text_line l;
    l.arena = a;
    l.buf = sc_buf_new(sc_arena_alloc(a, cap));
    l.failed = cap == 0 || l.buf.dst.len != cap;
    return l;
}

void text_lit(text_line *l, const uint8_t *lit, size_t n) {
    l->failed = l->failed || !sc_buf_lit(l->arena, &l->buf, lit, n);
}

/* A combining mark from the common blocks, which stacks on the character
 * before it. */
static bool combining(uint32_t cp) {
    return (cp >= 0x300 && cp <= 0x36f) || (cp >= 0x1ab0 && cp <= 0x1aff) ||
           (cp >= 0x1dc0 && cp <= 0x1dff) || (cp >= 0x20d0 && cp <= 0x20ff) ||
           (cp >= 0xfe20 && cp <= 0xfe2f);
}

void text_clean(text_line *l, const sc_arena *sa, sc_str s, size_t max) {
    size_t out = 0, marks = 0;
    for (size_t i = 0; i < s.len && !l->failed;) {
        sc_utf8_char c = sc_utf8_next(sa, s, i);
        if (c.len == 0) {
            l->failed = true; /* stale */
            return;
        }
        marks = c.valid && combining(c.cp) ? marks + 1u : 0;
        if (marks > TEXT_MARKS) {
            i += c.len; /* a tall stack would draw over the lines around it */
            continue;
        }
        bool control = c.valid && (c.cp < 0x20 || c.cp == 0x7f || (c.cp >= 0x80 && c.cp <= 0x9f));
        size_t n = !c.valid ? 3u : control ? 1u : c.len;
        if (n > max - out) {
            TEXT_LIT(l, "\xE2\x80\xA6"); /* show that the cap cut it */
            return;
        }
        if (!c.valid) {
            TEXT_LIT(l, "\xEF\xBF\xBD"); /* U+FFFD */
        } else if (control) {
            TEXT_LIT(l, " ");
        } else {
            l->failed = !sc_buf_str(l->arena, &l->buf, sa, sc_slice_sub(sa, s, i, c.len));
        }
        out += n;
        i += c.len;
    }
}

void text_u64(text_line *l, uint64_t n, bool sep) {
    l->failed = l->failed || !(sep ? sc_buf_u64_sep(l->arena, &l->buf, n)
                                   : sc_buf_u64(l->arena, &l->buf, n));
}

void text_ms(text_line *l, uint64_t ms) {
    l->failed = l->failed || !sc_buf_ms(l->arena, &l->buf, ms);
}

sc_str text_view(const text_line *l) {
    sc_str empty = {NULL, 0, l->arena->epoch};
    return l->failed ? empty : sc_buf_view(l->arena, l->buf);
}

sc_str text_clean_copy(sc_arena *a, const sc_arena *sa, sc_str s, size_t max) {
    text_line l = text_new(a, max + 3u);
    text_clean(&l, sa, s, max);
    return text_view(&l);
}

sc_str text_cut(sc_arena *a, const sc_arena *sa, sc_str s, size_t n) {
    sc_str keep = sc_utf8_truncate(sa, s, n);
    size_t room = 3u * keep.len; /* each byte may become U+FFFD */
    text_line l = text_new(a, room + 3u);
    text_clean(&l, sa, keep, room);
    TEXT_LIT(&l, "\xE2\x80\xA6");
    return text_view(&l);
}

/* "1 row" or "1,234 rows". */
static void text_rows(text_line *l, uint64_t n) {
    text_u64(l, n, true);
    if (n == 1) {
        TEXT_LIT(l, " row");
    } else {
        TEXT_LIT(l, " rows");
    }
}

query_terminal query_terminal_read(const sc_arena *a) {
    query_terminal t = {0};
    proto_header h;
    proto_reader r;
    if (a == NULL || !proto_read(a, &h, &r)) {
        return t;
    }
    for (uint32_t n = 0; n < TERMINAL_ITEMS; n++) {
        uint64_t kind = 0;
        if (!proto_get_u64(&r, &kind) || kind == PROTO_ITEM_END) {
            return t;
        }
        if (kind == PROTO_ITEM_TAG) {
            t.has_tag = proto_get_str(&r, &t.tag);
        } else if (kind == PROTO_ITEM_ERROR) {
            t.error = proto_get_str(&r, &t.sqlstate) && proto_get_str(&r, &t.message) &&
                      proto_get_str(&r, &t.detail) && proto_get_str(&r, &t.hint) &&
                      proto_get_u64(&r, &t.position);
        } else {
            return t;
        }
        if (r.failed) {
            return t;
        }
    }
    return t;
}

void query_connection(sc_arena *a, ui_conn_state state, bool broken, uint8_t *dot, sc_str *label) {
    switch (broken ? UI_CONN_DISCONNECTED : state) {
    case UI_CONN_CONNECTING:
        *dot = SHELL_CONNECTING;
        *label = SC_STR_LIT(a, "Connecting\xE2\x80\xA6");
        break;
    case UI_CONN_CONNECTED:
        *dot = SHELL_CONNECTED;
        *label = SC_STR_LIT(a, "Connected");
        break;
    case UI_CONN_AUTH_FAILED:
        *dot = SHELL_FAILED;
        *label = SC_STR_LIT(a, "Authentication failed");
        break;
    case UI_CONN_UNREACHABLE:
        *dot = SHELL_FAILED;
        *label = SC_STR_LIT(a, "Can't reach server");
        break;
    case UI_CONN_NONE:
        *dot = SHELL_OFFLINE;
        *label = SC_STR_LIT(a, "Not connected");
        break;
    default:
        *dot = SHELL_FAILED;
        *label = SC_STR_LIT(a, "Disconnected");
        break;
    }
}

sc_str query_count(sc_arena *a, const conn_ui *u, const sc_arena *ta, const query_terminal *t,
                   uint64_t elapsed_ms, uint8_t *status) {
    text_line c = text_new(a, TAG_SHOWN_BYTES + 96u);
    *status = SHELL_STATUS_NONE;
    switch (u->q) {
    case Q_NONE:
        TEXT_LIT(&c, "No rows");
        break;
    case Q_QUEUED:
    case Q_RUNNING:
        TEXT_LIT(&c, "Running\xE2\x80\xA6 ");
        text_ms(&c, elapsed_ms);
        break;
    case Q_STREAMING:
        text_rows(&c, u->rows);
        TEXT_LIT(&c, "\xE2\x80\xA6 ");
        text_ms(&c, elapsed_ms);
        break;
    case Q_CANCELLING:
        TEXT_LIT(&c, "Cancelling\xE2\x80\xA6");
        break;
    case Q_ROWS:
        text_rows(&c, u->rows);
        *status = SHELL_STATUS_OK;
        break;
    case Q_COMMAND_TAG:
        if (t->has_tag) {
            text_clean(&c, ta, t->tag, TAG_SHOWN_BYTES);
        } else {
            TEXT_LIT(&c, "Done");
        }
        *status = SHELL_STATUS_OK;
        break;
    case Q_ERROR:
        TEXT_LIT(&c, "Error");
        *status = SHELL_STATUS_ERROR;
        break;
    case Q_ERROR_AFTER_ROWS:
        text_rows(&c, u->rows);
        TEXT_LIT(&c, ", then an error");
        *status = SHELL_STATUS_ERROR;
        break;
    case Q_CAPPED:
        TEXT_LIT(&c, "First ");
        text_rows(&c, u->rows);
        if (u->cap == PROTO_CAP_LIMIT) {
            TEXT_LIT(&c, ", stopped at the row limit");
        } else {
            TEXT_LIT(&c, ", stopped at the memory limit");
        }
        *status = SHELL_STATUS_ERROR;
        break;
    case Q_CANCELLED:
        TEXT_LIT(&c, "Cancelled, ");
        text_rows(&c, u->rows);
        TEXT_LIT(&c, " kept");
        *status = SHELL_STATUS_ERROR;
        break;
    default:
        TEXT_LIT(&c, "Disconnected, ");
        text_rows(&c, u->rows);
        TEXT_LIT(&c, " kept");
        *status = SHELL_STATUS_ERROR;
        break;
    }
    return text_view(&c);
}

sc_str query_notice(sc_arena *a, uint8_t notice) {
    switch (notice) {
    case QUERY_NOTICE_TOO_BIG:
        return SC_STR_LIT(a, "That does not fit: the input box holds 64 KiB.");
    case QUERY_NOTICE_NOT_TEXT:
        return SC_STR_LIT(a, "Only UTF-8 text without control characters can be typed or pasted.");
    case QUERY_NOTICE_CLIP_TOO_BIG:
        return SC_STR_LIT(a, "The clipboard holds more than 64 KiB, so nothing was pasted.");
    case QUERY_NOTICE_BUSY:
        return SC_STR_LIT(a, "A query is running. Press Esc to cancel it first.");
    case QUERY_NOTICE_NOT_CONNECTED:
        return SC_STR_LIT(a, "Not connected, so the query was not sent.");
    case QUERY_NOTICE_POOL:
        return SC_STR_LIT(a, "No memory is free for another result.");
    default: {
        sc_str empty = {NULL, 0, a->epoch};
        return empty;
    }
    }
}
