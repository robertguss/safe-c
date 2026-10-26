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
/* Text at (x, y), clipped to w by h from there when both are above 0. */
static bool draw_text_in(frame_builder *b, sc_str t, uint8_t font, uint16_t size, int32_t x,
                         int32_t y, int32_t w, int32_t h, ui_color c) {
    if (t.len == 0) {
        return true;
    }
    ui_draw_item d = item(UI_DRAW_TEXT, x, y, w, h, c);
    d.text = t;
    d.font = font;
    d.font_size = size;
    return frame_draw(b, d);
}

static bool draw_text(frame_builder *b, sc_str t, uint8_t font, uint16_t size, int32_t x,
                      int32_t y, ui_color c) {
    return draw_text_in(b, t, font, size, x, y, 0, 0, c);
}

static uint32_t text_width(const sc_arena *a, sc_str t, uint8_t font, uint16_t size) {
    uint32_t w = 0, h = 0;
    if (t.len == 0 || !ui_text_size(a, t, font, size, &w, &h)) {
        return 0;
    }
    return w;
}

/* `s` (live in the frame arena) cut with an ellipsis to fit `max_w` points,
 * so no text widens its box: frame text is one line and does not wrap. */
static sc_str fit(sc_arena *fa, sc_str s, uint8_t font, uint16_t size, uint32_t max_w) {
    if (s.len == 0 || text_width(fa, s, font, size) <= max_w) {
        return s;
    }
    uint32_t ell = text_width(fa, SC_STR_LIT(fa, "\xE2\x80\xA6"), font, size);
    size_t lo = 0, hi = s.len - 1; /* the longest prefix that fits with the ellipsis */
    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1u) / 2u;
        if (text_width(fa, sc_slice_sub(fa, s, 0, mid), font, size) + ell <= max_w) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    return text_cut(fa, fa, s, lo);
}

/* The centre column's inner width: the window less the sidebar, insets and
 * borders. */
static uint32_t content_width(const grid_view *v) {
    uint32_t chrome = THEME_SIDEBAR_WIDTH + 3u * THEME_INSET + 2u;
    return v->width > chrome + 80u ? v->width - chrome : 80u;
}

/* A text node cut to `max_w` points. */
static bool fitted(frame_builder *b, bool dark, sc_str t, uint8_t font, uint16_t size,
                   uint32_t token, uint32_t max_w) {
    return text(b, dark, fit(b->arena, t, font, size, max_w), font, size, token);
}

static const sc_arena *terminal_arena(const conn_link *l) {
    return l->ui.terminal_kept ? link_arena(l, l->ui.terminal) : NULL;
}

/* ---- The error block ---- */

/* `ERROR <SQLSTATE>: <message>`, then DETAIL, HINT and position lines. */
static bool error_lines(frame_builder *b, bool dark, const sc_arena *ta, const query_terminal *t,
                        uint32_t max_w) {
    sc_arena *fa = b->arena;
    text_line head = text_new(fa, LINE_SHOWN_BYTES + 32u);
    TEXT_LIT(&head, "ERROR ");
    text_clean(&head, ta, t->sqlstate, 8);
    TEXT_LIT(&head, ": ");
    text_clean(&head, ta, t->message, LINE_SHOWN_BYTES);
    bool ok = fitted(b, dark, text_view(&head), UI_FONT_SANS_MEDIUM, THEME_TEXT_SIZE, THEME_ERROR, max_w);
    if (ok && t->detail.len > 0) {
        text_line d = text_new(fa, LINE_SHOWN_BYTES + 16u);
        TEXT_LIT(&d, "DETAIL: ");
        text_clean(&d, ta, t->detail, LINE_SHOWN_BYTES);
        ok = fitted(b, dark, text_view(&d), UI_FONT_SANS, THEME_TEXT_SIZE, THEME_TEXT, max_w);
    }
    if (ok && t->hint.len > 0) {
        text_line hn = text_new(fa, LINE_SHOWN_BYTES + 16u);
        TEXT_LIT(&hn, "HINT: ");
        text_clean(&hn, ta, t->hint, LINE_SHOWN_BYTES);
        ok = fitted(b, dark, text_view(&hn), UI_FONT_SANS, THEME_TEXT_SIZE, THEME_TEXT, max_w);
    }
    if (ok && t->position > 0) {
        text_line p = text_new(fa, 48);
        TEXT_LIT(&p, "at character ");
        text_u64(&p, t->position, false);
        ok = fitted(b, dark, text_view(&p), UI_FONT_SANS, THEME_TEXT_SIZE, THEME_SECONDARY_TEXT, max_w);
    }
    return ok;
}

/* The error replacing the result area, left-aligned at its top. */
static bool error_block(frame_builder *b, bool dark, const sc_arena *ta, const query_terminal *t,
                        uint32_t max_w) {
    ui_node box = padded(grow(UI_COLUMN), 12);
    box.gap = 4;
    return frame_open(b, box) && error_lines(b, dark, ta, t, max_w - 24u) && frame_close(b);
}

/* The error under rows that arrived before it, as a strip above the bottom bar. */
static bool error_strip(frame_builder *b, bool dark, const sc_arena *ta, const query_terminal *t,
                        uint32_t max_w) {
    ui_node strip = padded(frame_box(), 8);
    strip.width.mode = UI_SIZE_GROW;
    strip.direction = UI_COLUMN;
    strip.gap = 2;
    return rule(b, dark) && frame_open(b, strip) && error_lines(b, dark, ta, t, max_w - 16u) &&
           frame_close(b);
}

/* A secondary note under rows, as a strip above the bottom bar. */
static bool note_strip(frame_builder *b, bool dark, sc_str t, uint32_t max_w) {
    ui_node strip = padded(frame_box(), 8);
    strip.width.mode = UI_SIZE_GROW;
    return rule(b, dark) && frame_open(b, strip) &&
           fitted(b, dark, t, UI_FONT_SANS, THEME_TEXT_SIZE, THEME_SECONDARY_TEXT, max_w - 16u) &&
           frame_close(b);
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
    return text_clean_copy(fa, r->arena, c.bytes, CELL_SHOWN_BYTES);
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
        sc_str shown = text_clean_copy(fa, a, name, CELL_SHOWN_BYTES);
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
    uint64_t room = v->height / THEME_GRID_ROW_HEIGHT + 1u;
    uint64_t rows = l->ui.rows < GRID_MAX_ROWS ? l->ui.rows : GRID_MAX_ROWS;
    rows = rows < room ? rows : room;
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
             draw_text_in(b, fit(fa, name, UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE, w - 2u * CELL_PAD),
                          UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE, x + CELL_PAD, 3,
                          (int32_t)w - CELL_PAD, header - 3, theme_color(dark, THEME_SECONDARY_TEXT));
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
            /* Columns past the view's right edge are read but not drawn. */
            if (!ok || c >= cols.shown || x >= (int32_t)content_width(v)) {
                continue;
            }
            ok = SC_TSLICE_GET(uint32_t, fa, cols.widths, (size_t)c, &w) &&
                 draw_text_in(b,
                              fit(fa, cell_text(fa, &rc.r, cell), UI_FONT_SANS, THEME_TEXT_SIZE,
                                  w - 2u * CELL_PAD),
                              UI_FONT_SANS, THEME_TEXT_SIZE, x + CELL_PAD, y, (int32_t)w - CELL_PAD,
                              row_h - 4, theme_color(dark, cell.null ? THEME_SECONDARY_TEXT : THEME_TEXT));
            x += (int32_t)w;
        }
    }
    return ok && frame_custom(b, grow(UI_ROW));
}

/* ---- The input box ---- */

/* The text of the input box, a window of INPUT_LINES lines and as many
 * columns as fit that holds the cursor, and the cursor as a bar. After
 * select-all, each line is shaded. In the mono font a character is a column
 * as wide as "M"; a wide (CJK) glyph is the exception, and may push the line
 * end past the box. */
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
    uint32_t adv = text_width(fa, SC_STR_LIT(fa, "MMMMMMMMMM"), UI_FONT_MONO, THEME_TEXT_SIZE) / 10u;
    size_t cols = (content_width(v) - 2u * INPUT_PAD) / (adv > 0 ? adv : 8u);
    cols = cols > 8u ? cols : 8u;
    size_t skip = cur_col >= cols ? cur_col + 1u - cols : 0; /* scrolled columns */
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
            size_t from = input_advance(in, start, skip);
            size_t to = input_advance(in, from, cols + 1u);
            sc_str empty = {NULL, 0, fa->epoch};
            sc_str shown = to <= from ? empty
                                      : text_clean_copy(fa, in->arena,
                                                        sc_slice_sub(in->arena, all, from, to - from),
                                                        to - from);
            int32_t w = (int32_t)text_width(fa, shown, UI_FONT_MONO, THEME_TEXT_SIZE);
            ok = (!in->all || w == 0 ||
                  frame_draw(b, item(UI_DRAW_RECT, INPUT_PAD, y, w, INPUT_LINE_HEIGHT, shade))) &&
                 draw_text_in(b, shown, UI_FONT_MONO, THEME_TEXT_SIZE, INPUT_PAD, y + 1,
                              (int32_t)(content_width(v) - 2u * INPUT_PAD), INPUT_LINE_HEIGHT - 1, ink);
            if (ok && n == cur_line) {
                /* The box holds valid UTF-8 and a tab shows as one space, so
                 * the display copy has the box's bytes, unless it dropped a
                 * stack of more than TEXT_MARKS marks; then the bar sits a
                 * little to the right. */
                size_t off = in->cursor > from ? in->cursor - from : 0;
                size_t col = off < shown.len ? off : shown.len;
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

/* ---- The result area ---- */

static bool centred(frame_builder *b, bool dark, sc_str t, uint32_t max_w) {
    ui_node box = grow(UI_COLUMN);
    box.align_x = UI_ALIGN_CENTER;
    box.align_y = UI_ALIGN_CENTER;
    return frame_open(b, box) &&
           fitted(b, dark, t, UI_FONT_SANS, THEME_TEXT_SIZE, THEME_SECONDARY_TEXT, max_w - 24u) &&
           frame_close(b);
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
            ok = fitted(b, dark, text_clean_copy(fa, ma, part, LINE_SHOWN_BYTES), UI_FONT_SANS,
                        THEME_TEXT_SIZE, THEME_TEXT, content_width(v) - 24u);
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
    uint32_t cw = content_width(v);
    const sc_arena *ta = terminal_arena(l);
    query_terminal t = query_terminal_read(ta);
    if (l->ui.state == UI_CONN_AUTH_FAILED || l->ui.state == UI_CONN_UNREACHABLE) {
        return failure(b, v);
    }
    bool lost = q == Q_DISCONNECTED || l->ui.state == UI_CONN_DISCONNECTED || l->broken;
    if (l->ui.held_count > 0) {
        ui_node box = grow(UI_COLUMN);
        return frame_open(b, box) && rows_region(b, v) &&
               (!t.error || error_strip(b, dark, ta, &t, cw)) &&
               (!lost || note_strip(b, dark, SC_STR_LIT(fa, LOST_TEXT), cw)) && frame_close(b);
    }
    if (t.error) {
        return error_block(b, dark, ta, &t, cw);
    }
    if (q == Q_COMMAND_TAG && t.has_tag) {
        text_line s = text_new(fa, CELL_SHOWN_BYTES + 48u); /* + U+2026 and the suffix */
        text_clean(&s, ta, t.tag, CELL_SHOWN_BYTES);
        TEXT_LIT(&s, ": the statement ran and returned no rows.");
        return centred(b, dark, text_view(&s), cw);
    }
    if (lost) {
        return centred(b, dark, SC_STR_LIT(fa, LOST_TEXT), cw);
    }
    if (q == Q_CANCELLED) {
        return centred(b, dark, SC_STR_LIT(fa, "The query was cancelled."), cw);
    }
    if (conn_ui_active(&l->ui)) { /* a broken link reads as lost, above */
        return centred(b, dark, SC_STR_LIT(fa, "Running\xE2\x80\xA6"), cw);
    }
    if (l->ui.state == UI_CONN_CONNECTING) {
        return centred(b, dark, SC_STR_LIT(fa, "Connecting\xE2\x80\xA6"), cw);
    }
    return centred(b, dark, SC_STR_LIT(fa, "Run a query to see its results here."), cw);
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
    const sc_arena *ta = terminal_arena(v->link);
    query_terminal t = query_terminal_read(ta);
    text_line e = text_new(fa, 32);
    if (v->ran) {
        text_ms(&e, v->elapsed_ms);
    }
    query_connection(fa, v->link->ui.state, v->link->broken, &s.connection, &s.connection_label);
    conn_ui shown = v->link->ui;
    if (v->link->broken && conn_ui_active(&shown)) {
        shown.q = Q_DISCONNECTED; /* no reply will end it */
    }
    s.result_count = query_count(fa, &shown, ta, &t, v->elapsed_ms, &s.status);
    s.elapsed = text_view(&e);
    ui_node note = frame_box();
    note.pad_left = INPUT_PAD;
    note.pad_bottom = 4;
    sc_str notice = query_notice(fa, v->notice);
    return shell_open(b, &s) && input_region(b, v) &&
           (notice.len == 0 ||
            (frame_open(b, note) &&
             fitted(b, v->dark, notice, UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, THEME_ERROR,
                    content_width(v) - INPUT_PAD) &&
             frame_close(b))) &&
           rule(b, v->dark) && result_area(b, v) && shell_close(b, &s);
}
