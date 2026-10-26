#include "view_grid.h"
#include "sc_ui.h"
#include "theme.h"

#define CELL_SHOWN_BYTES 96u   /* a cell's display copy; a column is at most 300 points */
#define LINE_SHOWN_BYTES 400u  /* an error or message line's display copy */
#define FAILURE_LINES 8u       /* lines of a connect failure shown */
#define INPUT_LINES 6u         /* input box lines on screen */
#define INPUT_LINE_HEIGHT 18
#define INPUT_PAD 8
#define CELL_PAD 6
#define MIN_COLUMN_WIDTH 40u
#define MEASURED_ROWS 50u      /* rows measured for the column widths */
#define TERMINAL_ITEMS 100000u /* more items than a terminal arena can hold */

/* ---- Display text ---- */

/* A string being built in the frame arena. The first failure sticks. */
typedef struct {
    sc_arena *arena;
    sc_buf buf;
    bool failed;
} line;

static line line_new(sc_arena *a, size_t cap) {
    line l;
    l.arena = a;
    l.buf = sc_buf_new(sc_arena_alloc(a, cap));
    l.failed = cap == 0 || l.buf.dst.len != cap;
    return l;
}

static void line_lit(line *l, const uint8_t *lit, size_t n) {
    l->failed = l->failed || !sc_buf_lit(l->arena, &l->buf, lit, n);
}

#define LINE_LIT(l, lit) line_lit((l), (const uint8_t *)(lit), sizeof(lit) - 1)

/* Appends at most `max` bytes of `s` (live in `sa`), each control byte as a
 * space, since a frame's text is one line. Bytes from 0x80 up pass as they
 * are: text checks no encoding until the utf8 module (U9). */
static void line_clean(line *l, const sc_arena *sa, sc_str s, size_t max) {
    size_t n = s.len < max ? s.len : max;
    for (size_t i = 0; i < n && !l->failed; i++) {
        uint8_t c = 0;
        if (!sc_slice_get(sa, s, i, &c)) {
            l->failed = true;
            return;
        }
        if (c < 0x20 || c == 0x7f) {
            c = ' ';
        }
        line_lit(l, &c, 1);
    }
}

static void line_count(line *l, uint64_t n, bool sep) {
    l->failed = l->failed || !(sep ? sc_buf_u64_sep(l->arena, &l->buf, n)
                                   : sc_buf_u64(l->arena, &l->buf, n));
}

static void line_ms(line *l, uint64_t ms) {
    l->failed = l->failed || !sc_buf_ms(l->arena, &l->buf, ms);
}

/* The built string, or the canonical empty string after a failure. */
static sc_str line_view(const line *l) {
    sc_str empty = {NULL, 0, l->arena->epoch};
    return l->failed ? empty : sc_buf_view(l->arena, l->buf);
}

static sc_str clean(sc_arena *fa, const sc_arena *sa, sc_str s, size_t max) {
    line l = line_new(fa, max);
    line_clean(&l, sa, s, max);
    return line_view(&l);
}

/* "1 row" or "1,234 rows". */
static void line_rows(line *l, uint64_t n) {
    line_count(l, n, true);
    if (n == 1) {
        LINE_LIT(l, " row");
    } else {
        LINE_LIT(l, " rows");
    }
}

/* ---- Layout helpers ---- */

static ui_node grow(uint8_t direction) {
    ui_node n = frame_box();
    n.width.mode = UI_SIZE_GROW;
    n.height.mode = UI_SIZE_GROW;
    n.direction = direction;
    return n;
}

static ui_node padded(ui_node n, uint16_t p) {
    n.pad_left = p;
    n.pad_right = p;
    n.pad_top = p;
    n.pad_bottom = p;
    return n;
}

static bool text(frame_builder *b, bool dark, sc_str t, uint8_t font, uint16_t size,
                 uint32_t token) {
    return frame_text(b, t, font, size, theme_color(dark, token));
}

static bool rule(frame_builder *b, bool dark) {
    ui_node n = frame_box();
    n.width.mode = UI_SIZE_GROW;
    n.height.mode = UI_SIZE_FIXED;
    n.height.min = 1;
    n.background = theme_color(dark, THEME_SIDEBAR_BORDER);
    return frame_leaf(b, n);
}

static ui_draw_item item(uint8_t kind, int32_t x, int32_t y, int32_t w, int32_t h, ui_color c) {
    ui_draw_item d = {0};
    d.kind = kind;
    d.x = x;
    d.y = y;
    d.w = w;
    d.h = h;
    d.color = c;
    return d;
}

/* A text record; an empty string draws nothing and adds nothing. */
static bool draw_text(frame_builder *b, sc_str t, uint8_t font, uint16_t size, int32_t x,
                      int32_t y, ui_color c) {
    if (t.len == 0) {
        return true;
    }
    ui_draw_item d = item(UI_DRAW_TEXT, x, y, 0, 0, c);
    d.text = t;
    d.font = font;
    d.font_size = size;
    return frame_draw(b, d);
}

static uint32_t text_width(const sc_arena *a, sc_str t, uint8_t font, uint16_t size) {
    uint32_t w = 0, h = 0;
    if (t.len == 0 || !ui_text_size(a, t, font, size, &w, &h)) {
        return 0;
    }
    return w;
}

/* ---- The terminal reply ---- */

typedef struct {
    bool ok;
    bool has_tag;
    sc_str tag; /* the last statement's */
    bool error;
    sc_str sqlstate, message, detail, hint;
    uint64_t position;
} terminal;

/* Reads the terminal reply's items; the strings are views into its arena. */
static terminal read_terminal(const sc_arena *a) {
    terminal t = {0};
    proto_header h;
    proto_reader r;
    if (a == NULL || !proto_read(a, &h, &r)) {
        return t;
    }
    for (uint32_t n = 0; n < TERMINAL_ITEMS; n++) {
        uint64_t kind = 0, txn = 0;
        if (!proto_get_u64(&r, &kind)) {
            return t;
        }
        if (kind == PROTO_ITEM_END) {
            t.ok = proto_get_u64(&r, &txn);
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

static const sc_arena *terminal_arena(const conn_link *l) {
    return l->ui.terminal_kept ? link_arena(l, l->ui.terminal) : NULL;
}

/* ---- The error block ---- */

/* `ERROR <SQLSTATE>: <message>`, then DETAIL, HINT and position lines. */
static bool error_lines(frame_builder *b, bool dark, const sc_arena *ta, const terminal *t) {
    sc_arena *fa = b->arena;
    line head = line_new(fa, LINE_SHOWN_BYTES + 32u);
    LINE_LIT(&head, "ERROR ");
    line_clean(&head, ta, t->sqlstate, 8);
    LINE_LIT(&head, ": ");
    line_clean(&head, ta, t->message, LINE_SHOWN_BYTES);
    bool ok = text(b, dark, line_view(&head), UI_FONT_SANS_MEDIUM, THEME_TEXT_SIZE, THEME_ERROR);
    if (ok && t->detail.len > 0) {
        line d = line_new(fa, LINE_SHOWN_BYTES + 16u);
        LINE_LIT(&d, "DETAIL: ");
        line_clean(&d, ta, t->detail, LINE_SHOWN_BYTES);
        ok = text(b, dark, line_view(&d), UI_FONT_SANS, THEME_TEXT_SIZE, THEME_TEXT);
    }
    if (ok && t->hint.len > 0) {
        line hn = line_new(fa, LINE_SHOWN_BYTES + 16u);
        LINE_LIT(&hn, "HINT: ");
        line_clean(&hn, ta, t->hint, LINE_SHOWN_BYTES);
        ok = text(b, dark, line_view(&hn), UI_FONT_SANS, THEME_TEXT_SIZE, THEME_TEXT);
    }
    if (ok && t->position > 0) {
        line p = line_new(fa, 48);
        LINE_LIT(&p, "at character ");
        line_count(&p, t->position, false);
        ok = text(b, dark, line_view(&p), UI_FONT_SANS, THEME_TEXT_SIZE, THEME_SECONDARY_TEXT);
    }
    return ok;
}

/* The error replacing the result area, left-aligned at its top. */
static bool error_block(frame_builder *b, bool dark, const sc_arena *ta, const terminal *t) {
    ui_node box = padded(grow(UI_COLUMN), 12);
    box.gap = 4;
    return frame_open(b, box) && error_lines(b, dark, ta, t) && frame_close(b);
}

/* The error under rows that arrived before it, as a strip above the bottom bar. */
static bool error_strip(frame_builder *b, bool dark, const sc_arena *ta, const terminal *t) {
    ui_node strip = padded(frame_box(), 8);
    strip.width.mode = UI_SIZE_GROW;
    strip.direction = UI_COLUMN;
    strip.gap = 2;
    return rule(b, dark) && frame_open(b, strip) && error_lines(b, dark, ta, t) && frame_close(b);
}

/* A secondary note under rows, as a strip above the bottom bar. */
static bool note_strip(frame_builder *b, bool dark, sc_str t) {
    ui_node strip = padded(frame_box(), 8);
    strip.width.mode = UI_SIZE_GROW;
    return rule(b, dark) && frame_open(b, strip) &&
           text(b, dark, t, UI_FONT_SANS, THEME_TEXT_SIZE, THEME_SECONDARY_TEXT) && frame_close(b);
}

#define LOST_TEXT "The connection was lost. Quit and relaunch to reconnect."

/* ---- The rows ---- */

/* Walks the rows of the current set, chunk by chunk. */
typedef struct {
    const conn_link *l;
    size_t held;   /* the next held arena to read */
    uint64_t left; /* rows left in the current chunk */
    proto_reader r;
} row_cursor;

static bool row_next(row_cursor *rc) {
    while (rc->left == 0) {
        size_t i = conn_ui_held(&rc->l->ui, rc->held);
        if (i == SIZE_MAX) {
            return false;
        }
        rc->held++;
        proto_header h;
        const sc_arena *a = link_arena(rc->l, i);
        if (a == NULL || !proto_read(a, &h, &rc->r) || h.kind != PROTO_CHUNK) {
            return false;
        }
        rc->left = h.arg;
    }
    rc->left--;
    return true;
}

static sc_str cell_text(sc_arena *fa, const proto_reader *r, proto_cell c) {
    if (c.null) {
        return SC_STR_LIT(fa, "NULL");
    }
    return clean(fa, r->arena, c.bytes, CELL_SHOWN_BYTES);
}

typedef struct {
    uint64_t count;    /* columns in the set */
    size_t shown;      /* columns drawn */
    sc_tslice names;   /* sc_str, display copies */
    sc_tslice widths;  /* uint32_t, points */
} columns;

static bool read_columns(frame_builder *b, const conn_link *l, columns *out) {
    sc_arena *fa = b->arena;
    proto_header h;
    proto_reader r;
    const sc_arena *a = link_arena(l, conn_ui_held(&l->ui, 0));
    if (a == NULL || !proto_read(a, &h, &r) || h.kind != PROTO_COLUMNS ||
        !proto_get_u64(&r, &out->count)) {
        return false;
    }
    out->shown = out->count < GRID_MAX_COLUMNS ? (size_t)out->count : GRID_MAX_COLUMNS;
    out->names = SC_TSLICE_ALLOC(sc_str, fa, out->shown);
    out->widths = SC_TSLICE_ALLOC(uint32_t, fa, out->shown);
    if (sc_tslice_len(out->names) != out->shown || sc_tslice_len(out->widths) != out->shown) {
        return false;
    }
    for (size_t c = 0; c < out->shown; c++) {
        sc_str name;
        uint64_t oid = 0;
        if (!proto_get_str(&r, &name) || !proto_get_u64(&r, &oid)) {
            return false;
        }
        sc_str shown = clean(fa, a, name, CELL_SHOWN_BYTES);
        uint32_t w =
            text_width(fa, shown, UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE) + 2u * CELL_PAD;
        if (!SC_TSLICE_SET(sc_str, fa, out->names, c, &shown) ||
            !SC_TSLICE_SET(uint32_t, fa, out->widths, c, &w)) {
            return false;
        }
    }
    return true;
}

/* Widens each column to its widest cell in the first MEASURED_ROWS rows, up
 * to the theme's widest automatic column. */
static bool measure(frame_builder *b, const conn_link *l, columns *cols, uint64_t rows) {
    sc_arena *fa = b->arena;
    row_cursor rc = {l, 1, 0, {0}};
    for (uint64_t row = 0; row < rows && row < MEASURED_ROWS && row_next(&rc); row++) {
        for (uint64_t c = 0; c < cols->count; c++) {
            proto_cell cell;
            uint32_t w = 0;
            if (!proto_get_cell(&rc.r, &cell)) {
                return false;
            }
            if (c >= cols->shown || !SC_TSLICE_GET(uint32_t, fa, cols->widths, (size_t)c, &w)) {
                continue;
            }
            uint32_t cw = text_width(fa, cell_text(fa, &rc.r, cell), UI_FONT_SANS,
                                     THEME_TEXT_SIZE) + 2u * CELL_PAD;
            if (cw > w) {
                w = cw;
            }
            if (w > THEME_MAX_AUTO_COLUMN_WIDTH) {
                w = THEME_MAX_AUTO_COLUMN_WIDTH;
            }
            if (w < MIN_COLUMN_WIDTH) {
                w = MIN_COLUMN_WIDTH;
            }
            if (!SC_TSLICE_SET(uint32_t, fa, cols->widths, (size_t)c, &w)) {
                return false;
            }
        }
    }
    return true;
}

/* The header band and rows as one custom region (KTD3). */
static bool rows_region(frame_builder *b, const grid_view *v) {
    const conn_link *l = v->link;
    sc_arena *fa = b->arena;
    bool dark = v->dark;
    columns cols;
    uint64_t fit = v->height / THEME_GRID_ROW_HEIGHT + 1u;
    uint64_t rows = l->ui.rows < GRID_MAX_ROWS ? l->ui.rows : GRID_MAX_ROWS;
    rows = rows < fit ? rows : fit;
    if (!read_columns(b, l, &cols) || !measure(b, l, &cols, rows)) {
        return false;
    }
    int32_t total = 0;
    for (size_t c = 0; c < cols.shown; c++) {
        uint32_t w = 0;
        if (!SC_TSLICE_GET(uint32_t, fa, cols.widths, c, &w)) {
            return false;
        }
        total += (int32_t)w;
    }
    int32_t header = THEME_GRID_HEADER_HEIGHT;
    int32_t row_h = THEME_GRID_ROW_HEIGHT;
    int32_t bottom = header + (int32_t)rows * row_h;
    ui_color line_color = theme_color(dark, THEME_COLUMN_LINE);
    bool ok = frame_draw(b, item(UI_DRAW_RECT, 0, 0, total, header,
                                 theme_color(dark, THEME_GRID_HEADER))) &&
              frame_draw(b, item(UI_DRAW_LINE, 0, header, total, 0,
                                 theme_color(dark, THEME_HEADER_LINE)));
    for (uint64_t r = 1; ok && r < rows; r += 2) {
        ok = frame_draw(b, item(UI_DRAW_RECT, 0, header + (int32_t)r * row_h, total, row_h,
                                theme_color(dark, THEME_ALT_ROW)));
    }
    int32_t x = 0;
    for (size_t c = 0; ok && c < cols.shown; c++) {
        uint32_t w = 0;
        sc_str name;
        ok = SC_TSLICE_GET(uint32_t, fa, cols.widths, c, &w) &&
             SC_TSLICE_GET(sc_str, fa, cols.names, c, &name) &&
             draw_text(b, name, UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE, x + CELL_PAD, 3,
                       theme_color(dark, THEME_SECONDARY_TEXT));
        x += (int32_t)w;
        ok = ok && frame_draw(b, item(UI_DRAW_LINE, x - 1, 0, 0, bottom, line_color));
    }
    row_cursor rc = {l, 1, 0, {0}};
    for (uint64_t r = 0; ok && r < rows && row_next(&rc); r++) {
        int32_t y = header + (int32_t)r * row_h + 4;
        x = 0;
        for (uint64_t c = 0; ok && c < cols.count; c++) {
            proto_cell cell;
            uint32_t w = 0;
            ok = proto_get_cell(&rc.r, &cell);
            if (!ok || c >= cols.shown) {
                continue;
            }
            ok = SC_TSLICE_GET(uint32_t, fa, cols.widths, (size_t)c, &w) &&
                 draw_text(b, cell_text(fa, &rc.r, cell), UI_FONT_SANS, THEME_TEXT_SIZE,
                           x + CELL_PAD, y,
                           theme_color(dark, cell.null ? THEME_SECONDARY_TEXT : THEME_TEXT));
            x += (int32_t)w;
        }
    }
    return ok && frame_custom(b, grow(UI_ROW));
}

/* ---- The input box ---- */

/* The text of the input box, a window of INPUT_LINES lines that holds the
 * cursor, and the cursor as a bar. After select-all, each line is shaded. */
static bool input_region(frame_builder *b, const grid_view *v) {
    const input_box *in = v->input;
    sc_arena *fa = b->arena;
    bool dark = v->dark;
    sc_str all = input_text(in);
    size_t cur_line = 0, cur_col = 0;
    input_cursor_at(in, &cur_line, &cur_col);
    size_t first = cur_line >= INPUT_LINES ? cur_line - (INPUT_LINES - 1u) : 0;
    ui_color ink = theme_color(dark, THEME_TEXT);
    ui_color shade = theme_color(dark, THEME_ROW_SELECTION);
    bool ok = true;
    if (all.len == 0) {
        ok = draw_text(b, SC_STR_LIT(fa, "Type SQL, then press \xE2\x8C\x98\xE2\x86\xA9 (Ctrl+\xE2\x86\xA9) to run it."),
                       UI_FONT_MONO, THEME_TEXT_SIZE, INPUT_PAD + 2, INPUT_PAD,
                       theme_color(dark, THEME_SECONDARY_TEXT));
    }
    size_t start = 0, n = 0;
    for (size_t i = 0; ok && i <= all.len && n < first + INPUT_LINES; i++) {
        uint8_t c = '\n';
        if (i < all.len && !sc_slice_get(in->arena, all, i, &c)) {
            return false;
        }
        if (c != '\n') {
            continue;
        }
        if (n >= first) {
            int32_t y = INPUT_PAD + (int32_t)(n - first) * INPUT_LINE_HEIGHT;
            sc_str text_line = sc_slice_sub(in->arena, all, start, i - start);
            sc_str shown = clean(fa, in->arena, text_line, LINE_SHOWN_BYTES);
            int32_t w = (int32_t)text_width(fa, shown, UI_FONT_MONO, THEME_TEXT_SIZE);
            ok = (!in->all || w == 0 ||
                  frame_draw(b, item(UI_DRAW_RECT, INPUT_PAD, y, w, INPUT_LINE_HEIGHT, shade))) &&
                 draw_text(b, shown, UI_FONT_MONO, THEME_TEXT_SIZE, INPUT_PAD, y + 1, ink);
            if (ok && n == cur_line) {
                size_t col = cur_col < shown.len ? cur_col : shown.len;
                int32_t x = (int32_t)text_width(fa, sc_slice_sub(fa, shown, 0, col), UI_FONT_MONO,
                                                THEME_TEXT_SIZE);
                ok = frame_draw(b, item(UI_DRAW_LINE, INPUT_PAD + x, y + 1, 0,
                                        INPUT_LINE_HEIGHT - 2, theme_color(dark, THEME_ACCENT)));
            }
        }
        n++;
        start = i + 1;
    }
    ui_node box = frame_box();
    box.width.mode = UI_SIZE_GROW;
    box.height.mode = UI_SIZE_FIXED;
    box.height.min = (uint16_t)(2 * INPUT_PAD + INPUT_LINES * INPUT_LINE_HEIGHT);
    return ok && frame_custom(b, box);
}

static sc_str notice_text(sc_arena *fa, uint8_t notice) {
    switch (notice) {
    case GRID_NOTICE_TOO_BIG:
        return SC_STR_LIT(fa, "That does not fit: the input box holds 64 KiB.");
    case GRID_NOTICE_NOT_ASCII:
        return SC_STR_LIT(fa, "Only ASCII can be typed or pasted for now.");
    case GRID_NOTICE_CLIP_TOO_BIG:
        return SC_STR_LIT(fa, "The clipboard holds more than 64 KiB, so nothing was pasted.");
    case GRID_NOTICE_BUSY:
        return SC_STR_LIT(fa, "A query is running. Press Esc to cancel it first.");
    case GRID_NOTICE_NOT_CONNECTED:
        return SC_STR_LIT(fa, "Not connected, so the query was not sent.");
    case GRID_NOTICE_POOL:
        return SC_STR_LIT(fa, "No memory is free for another result.");
    default:
        return SC_STR_LIT(fa, "");
    }
}

/* ---- The result area ---- */

static bool centred(frame_builder *b, bool dark, sc_str t) {
    ui_node box = grow(UI_COLUMN);
    box.align_x = UI_ALIGN_CENTER;
    box.align_y = UI_ALIGN_CENTER;
    return frame_open(b, box) &&
           text(b, dark, t, UI_FONT_SANS, THEME_TEXT_SIZE, THEME_SECONDARY_TEXT) &&
           frame_close(b);
}

static bool active(query_state q) {
    return q == Q_QUEUED || q == Q_RUNNING || q == Q_STREAMING || q == Q_CANCELLING;
}

/* A failed connect: the state, libpq's message line by line, and the recovery. */
static bool failure(frame_builder *b, const grid_view *v) {
    const conn_link *l = v->link;
    sc_arena *fa = b->arena;
    bool dark = v->dark;
    bool auth = l->ui.state == UI_CONN_AUTH_FAILED;
    ui_node box = padded(grow(UI_COLUMN), 12);
    box.align_x = UI_ALIGN_CENTER;
    box.align_y = UI_ALIGN_CENTER;
    box.gap = 4;
    bool ok = frame_open(b, box) &&
              text(b, dark,
                   auth ? SC_STR_LIT(fa, "Authentication failed")
                        : SC_STR_LIT(fa, "Can't reach the server"),
                   UI_FONT_SANS_MEDIUM, THEME_TEXT_SIZE, THEME_ERROR);
    sc_str msg = l->failure;
    const sc_arena *ma = link_failure_arena(l);
    size_t start = 0, lines = 0;
    for (size_t i = 0; ok && ma != NULL && i <= msg.len && lines < FAILURE_LINES; i++) {
        uint8_t c = '\n';
        if (i < msg.len && !sc_slice_get(ma, msg, i, &c)) {
            break;
        }
        if (c != '\n') {
            continue;
        }
        if (i > start) {
            sc_str part = sc_slice_sub(ma, msg, start, i - start);
            ok = text(b, dark, clean(fa, ma, part, LINE_SHOWN_BYTES), UI_FONT_SANS,
                      THEME_TEXT_SIZE, THEME_TEXT);
            lines++;
        }
        start = i + 1;
    }
    return ok &&
           text(b, dark, SC_STR_LIT(fa, "Quit and relaunch the explorer to try again."),
                UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, THEME_SECONDARY_TEXT) &&
           frame_close(b);
}

static bool result_area(frame_builder *b, const grid_view *v) {
    const conn_link *l = v->link;
    sc_arena *fa = b->arena;
    bool dark = v->dark;
    query_state q = l->ui.q;
    const sc_arena *ta = terminal_arena(l);
    terminal t = read_terminal(ta);
    if (l->ui.state == UI_CONN_AUTH_FAILED || l->ui.state == UI_CONN_UNREACHABLE) {
        return failure(b, v);
    }
    bool lost = q == Q_DISCONNECTED || l->ui.state == UI_CONN_DISCONNECTED;
    if (l->ui.held_count > 0) {
        ui_node box = grow(UI_COLUMN);
        return frame_open(b, box) && rows_region(b, v) &&
               (!t.error || error_strip(b, dark, ta, &t)) &&
               (!lost || note_strip(b, dark, SC_STR_LIT(fa, LOST_TEXT))) && frame_close(b);
    }
    if (t.error) {
        return error_block(b, dark, ta, &t);
    }
    if (q == Q_COMMAND_TAG && t.has_tag) {
        line s = line_new(fa, CELL_SHOWN_BYTES + 32u);
        line_clean(&s, ta, t.tag, CELL_SHOWN_BYTES);
        LINE_LIT(&s, ": the statement ran and returned no rows.");
        return centred(b, dark, line_view(&s));
    }
    if (lost) {
        return centred(b, dark, SC_STR_LIT(fa, LOST_TEXT));
    }
    if (q == Q_CANCELLED) {
        return centred(b, dark, SC_STR_LIT(fa, "The query was cancelled."));
    }
    if (active(q)) {
        return centred(b, dark, SC_STR_LIT(fa, "Running\xE2\x80\xA6"));
    }
    if (l->ui.state == UI_CONN_CONNECTING) {
        return centred(b, dark, SC_STR_LIT(fa, "Connecting\xE2\x80\xA6"));
    }
    return centred(b, dark, SC_STR_LIT(fa, "Run a query to see its results here."));
}

/* ---- Toolbar and bottom bar ---- */

static void connection(sc_arena *fa, const conn_link *l, shell_state *s) {
    switch (l->broken ? UI_CONN_DISCONNECTED : l->ui.state) {
    case UI_CONN_CONNECTING:
        s->connection = SHELL_CONNECTING;
        s->connection_label = SC_STR_LIT(fa, "Connecting\xE2\x80\xA6");
        break;
    case UI_CONN_CONNECTED:
        s->connection = SHELL_CONNECTED;
        s->connection_label = SC_STR_LIT(fa, "Connected");
        break;
    case UI_CONN_AUTH_FAILED:
        s->connection = SHELL_FAILED;
        s->connection_label = SC_STR_LIT(fa, "Authentication failed");
        break;
    case UI_CONN_UNREACHABLE:
        s->connection = SHELL_FAILED;
        s->connection_label = SC_STR_LIT(fa, "Can't reach server");
        break;
    case UI_CONN_NONE:
        s->connection = SHELL_OFFLINE;
        s->connection_label = SC_STR_LIT(fa, "Not connected");
        break;
    default:
        s->connection = SHELL_FAILED;
        s->connection_label = SC_STR_LIT(fa, "Disconnected");
        break;
    }
}

/* The bottom bar's count or tag, tick or cross, and time. */
static void bottom(sc_arena *fa, const grid_view *v, shell_state *s) {
    const conn_ui *u = &v->link->ui;
    const sc_arena *ta = terminal_arena(v->link);
    terminal t = read_terminal(ta);
    line c = line_new(fa, CELL_SHOWN_BYTES + 64u);
    s->status = SHELL_STATUS_NONE;
    switch (u->q) {
    case Q_NONE:
        LINE_LIT(&c, "No rows");
        break;
    case Q_QUEUED:
    case Q_RUNNING:
        LINE_LIT(&c, "Running\xE2\x80\xA6 ");
        line_ms(&c, v->elapsed_ms);
        break;
    case Q_STREAMING:
        line_rows(&c, u->rows);
        LINE_LIT(&c, "\xE2\x80\xA6 ");
        line_ms(&c, v->elapsed_ms);
        break;
    case Q_CANCELLING:
        LINE_LIT(&c, "Cancelling\xE2\x80\xA6");
        break;
    case Q_ROWS:
        line_rows(&c, u->rows);
        s->status = SHELL_STATUS_OK;
        break;
    case Q_COMMAND_TAG:
        line_clean(&c, ta, t.tag, CELL_SHOWN_BYTES);
        s->status = SHELL_STATUS_OK;
        break;
    case Q_ERROR:
        LINE_LIT(&c, "Error");
        s->status = SHELL_STATUS_ERROR;
        break;
    case Q_ERROR_AFTER_ROWS:
        line_rows(&c, u->rows);
        LINE_LIT(&c, ", then an error");
        s->status = SHELL_STATUS_ERROR;
        break;
    case Q_CAPPED:
        LINE_LIT(&c, "First ");
        line_rows(&c, u->rows);
        if (u->cap == PROTO_CAP_LIMIT) {
            LINE_LIT(&c, ", stopped at the row limit");
        } else {
            LINE_LIT(&c, ", stopped at the memory limit");
        }
        s->status = SHELL_STATUS_ERROR;
        break;
    case Q_CANCELLED:
        LINE_LIT(&c, "Cancelled, ");
        line_rows(&c, u->rows);
        LINE_LIT(&c, " kept");
        s->status = SHELL_STATUS_ERROR;
        break;
    default:
        LINE_LIT(&c, "Disconnected, ");
        line_rows(&c, u->rows);
        LINE_LIT(&c, " kept");
        s->status = SHELL_STATUS_ERROR;
        break;
    }
    s->result_count = line_view(&c);
    line e = line_new(fa, 32);
    if (v->ran) {
        line_ms(&e, v->elapsed_ms);
    }
    s->elapsed = line_view(&e);
}

bool grid_build(frame_builder *b, const grid_view *v) {
    sc_arena *fa = b->arena;
    shell_state s;
    s.dark = v->dark;
    s.show_sidebar = true;
    s.object = SC_STR_LIT(fa, "Query");
    s.server = v->custom_conninfo ? SC_STR_LIT(fa, "--conninfo connection")
                                  : SC_STR_LIT(fa, "PG* environment");
    s.result_label = SC_STR_LIT(fa, "Query Result");
    s.placeholder = SC_STR_LIT(fa, "");
    connection(fa, v->link, &s);
    bottom(fa, v, &s);
    ui_node note = frame_box();
    note.pad_left = INPUT_PAD;
    note.pad_bottom = 4;
    sc_str notice = notice_text(fa, v->notice);
    return shell_open(b, &s) && input_region(b, v) &&
           (notice.len == 0 ||
            (frame_open(b, note) &&
             text(b, v->dark, notice, UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, THEME_ERROR) &&
             frame_close(b))) &&
           rule(b, v->dark) && result_area(b, v) && shell_close(b, &s);
}
