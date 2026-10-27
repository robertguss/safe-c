#include "view_grid.h"
#include "detail.h"
#include "focus.h"
#include "../../adapters/sc_ui.h"
#include "theme.h"

#define CELL_SHOWN_BYTES 96u   /* a cell's display copy; a column is at most 300 points */
#define LINE_SHOWN_BYTES 400u  /* an error or message line's display copy */
#define FAILURE_LINES 8u       /* lines of a connect failure shown */
#define INPUT_LINES 6u         /* input box lines on screen */
#define INPUT_LINE_HEIGHT 18
#define INPUT_PAD 8
#define CELL_PAD 6
#define MIN_COLUMN_WIDTH 40u
#define MEASURED_ROWS 100u     /* first-chunk rows measured for the column widths, once a set */
#define NOTICE_HEIGHT 18u      /* the note under the input box */
#define DETAIL_LINES 12u       /* lines of one value in the detail pane */
#define DETAIL_LINE_H 18u

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
static sc_str fit(frame_builder *b, sc_str s, uint8_t font, uint16_t size, uint32_t max_w) {
    sc_arena *fa = b->arena;
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
    return text_cut(fa, fa, s, lo, &b->failed);
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
    return text(b, dark, fit(b, t, font, size, max_w), font, size, token);
}

static const sc_arena *terminal_arena(const grid_view *v) {
    return v->ui->terminal_kept ? link_arena(v->link, v->ui->terminal) : NULL;
}

/* ---- The error block ---- */

/* `ERROR <SQLSTATE>: <message>`, then DETAIL, HINT and position lines. */
static bool error_lines(frame_builder *b, bool dark, const sc_arena *ta, const query_terminal *t,
                        uint32_t max_w) {
    sc_arena *fa = b->arena;
    text_line head = text_new(fa, LINE_SHOWN_BYTES + 32u, &b->failed);
    TEXT_LIT(&head, "ERROR ");
    text_clean(&head, ta, t->sqlstate, 8);
    TEXT_LIT(&head, ": ");
    text_clean(&head, ta, t->message, LINE_SHOWN_BYTES);
    bool ok = fitted(b, dark, text_view(&head), UI_FONT_SANS_MEDIUM, THEME_TEXT_SIZE, THEME_ERROR, max_w);
    if (ok && t->detail.len > 0) {
        text_line d = text_new(fa, LINE_SHOWN_BYTES + 16u, &b->failed);
        TEXT_LIT(&d, "DETAIL: ");
        text_clean(&d, ta, t->detail, LINE_SHOWN_BYTES);
        ok = fitted(b, dark, text_view(&d), UI_FONT_SANS, THEME_TEXT_SIZE, THEME_TEXT, max_w);
    }
    if (ok && t->hint.len > 0) {
        text_line hn = text_new(fa, LINE_SHOWN_BYTES + 16u, &b->failed);
        TEXT_LIT(&hn, "HINT: ");
        text_clean(&hn, ta, t->hint, LINE_SHOWN_BYTES);
        ok = fitted(b, dark, text_view(&hn), UI_FONT_SANS, THEME_TEXT_SIZE, THEME_TEXT, max_w);
    }
    if (ok && t->position > 0) {
        text_line p = text_new(fa, 48, &b->failed);
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

static sc_str cell_text(frame_builder *b, const sc_arena *a, proto_cell c) {
    if (c.null) {
        return SC_STR_LIT(b->arena, "NULL");
    }
    sc_str bytes = c.truncated ? text_whole_chars(a, c.bytes) : c.bytes;
    return text_cell_copy(b->arena, a, bytes, CELL_SHOWN_BYTES, c.truncated, &b->failed);
}

void grid_state_init(grid_state *g, sc_arena *wa, sc_arena *scratch) {
    grid_state z = {0};
    *g = z;
    g->wa = wa;
    g->scratch = scratch;
    g->widths = SC_TSLICE_ALLOC(uint32_t, wa, GRID_MAX_COLUMNS);
}

/* result_measure_fn: a display copy, measured in the font it is drawn in.
 * `ctx` is the frame builder. */
static uint32_t measure_text(void *ctx, const sc_arena *a, sc_str s, result_text kind) {
    frame_builder *b = ctx;
    sc_arena *fa = b->arena;
    if (kind == RESULT_TEXT_NULL) {
        return text_width(fa, SC_STR_LIT(fa, "NULL"), UI_FONT_SANS, THEME_TEXT_SIZE);
    }
    sc_str shown = kind == RESULT_TEXT_HEADER
                       ? text_clean_copy(fa, a, s, CELL_SHOWN_BYTES, &b->failed)
                       : text_cell_copy(fa, a, s, CELL_SHOWN_BYTES, false, &b->failed);
    return kind == RESULT_TEXT_HEADER
               ? text_width(fa, shown, UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE)
               : text_width(fa, shown, UI_FONT_SANS, THEME_TEXT_SIZE);
}

/* Brings the width cache up to the current set: measured once the first
 * chunk is in hand, from the header and its first MEASURED_ROWS rows, at
 * least MIN_COLUMN_WIDTH. A new set also resets the scroll and selection. */
static bool sync_widths(frame_builder *b, const grid_view *v, grid_state *g) {
    const result *res = v->res;
    bool same = g->req == v->res_req && g->sets == v->res_sets;
    if (same && g->cached) {
        return true;
    }
    if (!same) {
        grid_sel z = {0};
        g->sel = z;
        g->req = v->res_req;
        g->sets = v->res_sets;
    }
    g->shown = res->cols < GRID_MAX_COLUMNS ? (size_t)res->cols : GRID_MAX_COLUMNS;
    g->total = 0;
    g->cached = res->count > 0;
    if (g->shown == 0) {
        return true;
    }
    if (sc_tslice_len(g->widths) < g->shown ||
        !result_widths(res, g->wa, g->widths, g->shown,
                       (result_width_rule){measure_text, b, MEASURED_ROWS, 2u * CELL_PAD,
                                           THEME_MAX_AUTO_COLUMN_WIDTH})) {
        g->cached = false;
        return false;
    }
    for (size_t c = 0; c < g->shown; c++) {
        uint32_t w = 0;
        if (!SC_TSLICE_GET(uint32_t, g->wa, g->widths, c, &w)) {
            return false;
        }
        w = w < MIN_COLUMN_WIDTH ? MIN_COLUMN_WIDTH : w;
        g->total += w;
        if (!SC_TSLICE_SET(uint32_t, g->wa, g->widths, c, &w)) {
            return false;
        }
    }
    return true;
}

/* The grid region's height: the window less the toolbar, the input box, the
 * notice, the bottom bar, the insets and any strip under the rows. */
static uint32_t grid_height(const grid_view *v, uint32_t strip) {
    uint32_t chrome = THEME_TOOLBAR_HEIGHT + TAB_STRIP_HEIGHT + THEME_BOTTOM_BAR_HEIGHT +
                      2u * THEME_INSET + 4u +
                      2u * INPUT_PAD + INPUT_LINES * INPUT_LINE_HEIGHT +
                      (v->notice != QUERY_NOTICE_NONE ? NOTICE_HEIGHT : 0u) + strip;
    return v->height > chrome + 40u ? v->height - chrome : 40u;
}

/* The header band and the visible rows as one custom region (KTD3): only the
 * rows and columns the viewport shows are read and laid out. */
static bool rows_region(frame_builder *b, const grid_view *v, uint32_t w_pts, uint32_t h_pts) {
    grid_state *g = v->grid;
    sc_arena *fa = b->arena;
    bool dark = v->dark;
    const result *res = v->res;
    if (!sync_widths(b, v, g)) {
        return false;
    }
    grid_geom geo = {res->rows, THEME_GRID_ROW_HEIGHT, THEME_GRID_HEADER_HEIGHT, w_pts, h_pts,
                     g->sel.g.scroll_y, g->sel.g.scroll_x, g->total};
    g->sel.g = grid_clamp(geo);
    if (g->sel.selected && g->sel.row >= res->rows) {
        g->sel.selected = false;
    }
    geo = g->sel.g;
    grid_rows vis = grid_visible(geo);
    grid_cols vc;
    if (!grid_columns(geo, g->wa, g->widths, g->shown, &vc)) {
        return false;
    }
    int32_t header = THEME_GRID_HEADER_HEIGHT;
    int32_t row_h = THEME_GRID_ROW_HEIGHT;
    int32_t span = (int32_t)(g->total - geo.scroll_x); /* the right end of the last column */
    span = span < (int32_t)w_pts ? span : (int32_t)w_pts;
    int32_t bottom = header + (int32_t)vis.top + (int32_t)vis.count * row_h;
    bool ok = true;
    /* Row backgrounds: alternate by absolute row, and the selection. */
    for (uint64_t r = 0; ok && r < vis.count; r++) {
        uint64_t row = vis.first + r;
        int32_t y = header + (int32_t)vis.top + (int32_t)r * row_h;
        bool sel = g->sel.selected && g->sel.row == row;
        if (sel || row % 2u == 1u) {
            ok = frame_draw(b, item(UI_DRAW_RECT, 0, y, span, row_h,
                                    theme_color(dark, sel ? THEME_ROW_SELECTION : THEME_ALT_ROW)));
        }
    }
    /* Cells: read every cell of a visible row in order, draw the visible columns. */
    result_cursor rc;
    ok = ok && (vis.count == 0 || result_seek(&rc, res, vis.first));
    for (uint64_t r = 0; ok && r < vis.count && (r == 0 || result_next_row(&rc)); r++) {
        uint64_t row = vis.first + r;
        bool sel = g->sel.selected && g->sel.row == row;
        int32_t y = header + (int32_t)vis.top + (int32_t)r * row_h + 4;
        int32_t x = (int32_t)vc.left;
        for (size_t c = 0; ok && c < vc.end; c++) {
            proto_cell cell;
            ok = result_next_cell(&rc, &cell);
            if (!ok || c < vc.first) {
                continue;
            }
            uint32_t w = 0;
            sc_str name;
            uint32_t oid = 0;
            ok = SC_TSLICE_GET(uint32_t, g->wa, g->widths, c, &w) && result_column(res, c, &name, &oid);
            if (!ok) {
                break;
            }
            sc_str full = cell_text(b, rc.rd.arena, cell);
            /* A number wider than its column, past the rows measured, widens
             * it for the next frame: a cut number misleads. */
            if (result_numeric(oid) && !cell.null) {
                uint32_t need = text_width(fa, full, UI_FONT_SANS, THEME_TEXT_SIZE) + 2u * CELL_PAD;
                g->grew = grid_widen(g->wa, g->widths, c, need, THEME_MAX_AUTO_COLUMN_WIDTH,
                                     &g->total) || g->grew;
            }
            sc_str t = fit(b, full, UI_FONT_SANS, THEME_TEXT_SIZE, w - 2u * CELL_PAD);
            /* Numbers sit at the right edge of their column. */
            int32_t at = CELL_PAD;
            if (result_numeric(oid) && !cell.null) {
                at = (int32_t)w - CELL_PAD - (int32_t)text_width(fa, t, UI_FONT_SANS, THEME_TEXT_SIZE);
            }
            uint32_t ink = sel ? THEME_SELECTED_TEXT : cell.null ? THEME_SECONDARY_TEXT : THEME_TEXT;
            ok = draw_text_in(b, t, UI_FONT_SANS, THEME_TEXT_SIZE, x + at, y, (int32_t)w - at,
                              row_h - 4, theme_color(dark, ink));
            x += (int32_t)w;
        }
    }
    /* The header band last, over a row scrolled half under it. */
    ui_color line_color = theme_color(dark, THEME_COLUMN_LINE);
    ok = ok && frame_draw(b, item(UI_DRAW_RECT, 0, 0, span, header, theme_color(dark, THEME_GRID_HEADER))) &&
         frame_draw(b, item(UI_DRAW_LINE, 0, header, span, 0, theme_color(dark, THEME_HEADER_LINE)));
    int32_t x = (int32_t)vc.left;
    for (size_t c = vc.first; ok && c < vc.end; c++) {
        uint32_t w = 0;
        sc_str name;
        uint32_t oid = 0;
        ok = SC_TSLICE_GET(uint32_t, g->wa, g->widths, c, &w) && result_column(res, c, &name, &oid) &&
             draw_text_in(b, fit(b, text_clean_copy(fa, res->col_arena, name, CELL_SHOWN_BYTES, &b->failed),
                                 UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE, w - 2u * CELL_PAD),
                          UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE, x + CELL_PAD, 3,
                          (int32_t)w - CELL_PAD, header - 3, theme_color(dark, THEME_SECONDARY_TEXT));
        x += (int32_t)w;
        ok = ok && frame_draw(b, item(UI_DRAW_LINE, x - 1, 0, 0, bottom > header ? bottom : header,
                                      line_color));
    }
    ui_node region = frame_box();
    region.width.mode = UI_SIZE_GROW;
    region.height.mode = UI_SIZE_FIXED;
    region.height.min = (uint16_t)h_pts;
    region.background = theme_color(dark, THEME_CONTENT);
    region.tag = FOCUS_TAG_GRID;
    return ok && frame_custom(b, region);
}

/* ---- The row-detail pane ---- */

/* detail_measure_fn. Each call reuses one small scratch arena, so wrapping
 * costs no frame memory; a line longer than LINE_SHOWN_BYTES counts as too
 * wide, which also ends a run of zero-width characters. */
static uint32_t detail_measure(void *ctx, const sc_arena *a, sc_str s) {
    sc_arena *sa = ctx;
    bool lost = false;
    if (s.len > LINE_SHOWN_BYTES || !sc_arena_reset(sa)) {
        return UINT32_MAX;
    }
    uint32_t w = text_width(sa, text_clean_copy(sa, a, s, LINE_SHOWN_BYTES, &lost), UI_FONT_SANS,
                            THEME_TEXT_SIZE);
    return lost ? UINT32_MAX : w;
}

/* The selected row's columns: each name with its type in grey, then the
 * value wrapped to the pane, NULL greyed, and a note for a truncated value.
 * Columns stop when the pane is full. */
static bool detail_pane(frame_builder *b, const grid_view *v, uint32_t h_pts) {
    grid_state *g = v->grid;
    const result *res = v->res;
    sc_arena *fa = b->arena;
    bool dark = v->dark;
    ui_node pane = padded(frame_box(), 10);
    pane.direction = UI_COLUMN;
    pane.width.mode = UI_SIZE_FIXED;
    pane.width.min = GRID_DETAIL_WIDTH;
    pane.height.mode = UI_SIZE_FIXED;
    pane.height.min = (uint16_t)h_pts;
    pane.gap = 2;
    pane.border = 1;
    pane.border_color = theme_color(dark, THEME_SIDEBAR_BORDER);
    pane.tag = FOCUS_TAG_DETAIL;
    uint32_t inner = GRID_DETAIL_WIDTH - 22u;
    if (!frame_open(b, pane)) {
        return false;
    }
    if (!g->sel.selected) {
        return text(b, dark, SC_STR_LIT(fa, "Select a row to see its values."), UI_FONT_SANS,
                    THEME_SMALL_TEXT_SIZE, THEME_SECONDARY_TEXT) &&
               frame_close(b);
    }
    result_cursor rc;
    bool ok = result_seek(&rc, res, g->sel.row);
    uint32_t used = 20;
    sc_tslice lines = SC_TSLICE_ALLOC(sc_str, fa, DETAIL_LINES);
    uint64_t c = 0;
    for (; ok && c < res->cols && used + 3u * DETAIL_LINE_H <= h_pts; c++) {
        proto_cell cell;
        sc_str name;
        uint32_t oid = 0;
        ok = result_next_cell(&rc, &cell) && result_column(res, c, &name, &oid);
        if (!ok) {
            break;
        }
        ui_node head = frame_box();
        head.gap = 6;
        ok = frame_open(b, head) &&
             fitted(b, dark, text_clean_copy(fa, res->col_arena, name, CELL_SHOWN_BYTES, &b->failed),
                    UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE, THEME_TEXT, inner / 2u) &&
             fitted(b, dark, detail_type_name(fa, oid, &b->failed), UI_FONT_SANS,
                    THEME_SMALL_TEXT_SIZE, THEME_SECONDARY_TEXT, inner / 2u) &&
             frame_close(b);
        used += DETAIL_LINE_H;
        if (!ok) {
            break;
        }
        if (cell.null) {
            ok = text(b, dark, detail_note(fa, cell, &b->failed), UI_FONT_SANS, THEME_TEXT_SIZE,
                      THEME_SECONDARY_TEXT);
            used += DETAIL_LINE_H;
        } else {
            size_t n = 0;
            bool more = false;
            sc_str bytes = cell.truncated ? text_whole_chars(rc.rd.arena, cell.bytes) : cell.bytes;
            ok = detail_wrap(rc.rd.arena, bytes, inner, detail_measure, g->scratch, fa, lines, &n,
                             &more);
            for (size_t i = 0; ok && i < n && used + DETAIL_LINE_H <= h_pts; i++) {
                sc_str part;
                ok = SC_TSLICE_GET(sc_str, fa, lines, i, &part);
                sc_str shown = text_clean_copy(fa, rc.rd.arena, part, LINE_SHOWN_BYTES, &b->failed);
                if (ok && more && i + 1u == n) {
                    shown = text_cut(fa, fa, shown, shown.len, &b->failed); /* lines were left over */
                }
                if (ok && shown.len == 0) {
                    shown = SC_STR_LIT(fa, " "); /* an empty line keeps its height */
                }
                if (ok && i + 1u < n && used + 2u * DETAIL_LINE_H > h_pts) {
                    shown = text_cut(fa, fa, shown, shown.len, &b->failed); /* the pane cut the value */
                }
                ok = ok && fitted(b, dark, shown, UI_FONT_SANS, THEME_TEXT_SIZE, THEME_TEXT, inner);
                used += DETAIL_LINE_H;
            }
        }
        if (ok && cell.truncated) {
            ok = text(b, dark, detail_note(fa, cell, &b->failed), UI_FONT_SANS,
                      THEME_SMALL_TEXT_SIZE, THEME_SECONDARY_TEXT);
            used += DETAIL_LINE_H;
        }
        used += 6;
    }
    if (ok && c < res->cols) {
        text_line more = text_new(fa, 48, &b->failed);
        TEXT_LIT(&more, "+");
        text_u64(&more, res->cols - c, true);
        if (res->cols - c == 1u) {
            TEXT_LIT(&more, " more column");
        } else {
            TEXT_LIT(&more, " more columns");
        }
        ok = text(b, dark, text_view(&more), UI_FONT_SANS, THEME_SMALL_TEXT_SIZE,
                  THEME_SECONDARY_TEXT);
    }
    return ok && frame_close(b);
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
                                                        to - from, &b->failed);
            int32_t w = (int32_t)text_width(fa, shown, UI_FONT_MONO, THEME_TEXT_SIZE);
            ok = (!in->all || w == 0 ||
                  frame_draw(b, item(UI_DRAW_RECT, INPUT_PAD, y, w, INPUT_LINE_HEIGHT, shade))) &&
                 draw_text_in(b, shown, UI_FONT_MONO, THEME_TEXT_SIZE, INPUT_PAD, y + 1,
                              (int32_t)(content_width(v) - 2u * INPUT_PAD), INPUT_LINE_HEIGHT - 1, ink);
            if (ok && v->editor_focused && n == cur_line) {
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
    box.tag = FOCUS_TAG_EDITOR;
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
            ok = fitted(b, dark, text_clean_copy(fa, ma, part, LINE_SHOWN_BYTES, &b->failed), UI_FONT_SANS,
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
    query_state q = v->ui->q;
    uint32_t cw = content_width(v);
    const sc_arena *ta = terminal_arena(v);
    query_terminal t = query_terminal_read(ta);
    if (l->ui.state == UI_CONN_AUTH_FAILED || l->ui.state == UI_CONN_UNREACHABLE) {
        return failure(b, v);
    }
    bool lost = q == Q_DISCONNECTED || l->ui.state == UI_CONN_DISCONNECTED || l->broken;
    if (v->ui->held_count > 0) {
        /* The strips under the rows, at 16 points a line plus padding. */
        uint32_t lines = t.error ? 1u + (t.detail.len > 0) + (t.hint.len > 0) + (t.position > 0) : 0;
        uint32_t strip = (t.error ? 17u + 18u * lines - 2u : 0u) + (lost ? 33u : 0u);
        uint32_t h = grid_height(v, strip);
        bool pane = v->grid->detail;
        uint32_t w = cw > GRID_DETAIL_WIDTH + 80u && pane ? cw - GRID_DETAIL_WIDTH : cw;
        ui_node box = grow(UI_COLUMN);
        ui_node row = frame_box();
        row.width.mode = UI_SIZE_GROW;
        row.height.mode = UI_SIZE_FIXED;
        row.height.min = (uint16_t)h;
        return frame_open(b, box) && frame_open(b, row) && rows_region(b, v, w, h) &&
               (!pane || detail_pane(b, v, h)) && frame_close(b) &&
               (!t.error || error_strip(b, dark, ta, &t, cw)) &&
               (!lost || note_strip(b, dark, SC_STR_LIT(fa, LOST_TEXT), cw)) && frame_close(b);
    }
    if (t.error) {
        return error_block(b, dark, ta, &t, cw);
    }
    if (q == Q_COMMAND_TAG && t.has_tag) {
        text_line s = text_new(fa, CELL_SHOWN_BYTES + 48u, &b->failed); /* + U+2026 and the suffix */
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
    if (v->waiting) {
        return centred(b, dark, SC_STR_LIT(fa, "Waiting for connection\xE2\x80\xA6"), cw);
    }
    if (conn_ui_active(v->ui)) { /* a broken link reads as lost, above */
        return centred(b, dark, SC_STR_LIT(fa, "Running\xE2\x80\xA6"), cw);
    }
    if (l->ui.state == UI_CONN_CONNECTING) {
        return centred(b, dark, SC_STR_LIT(fa, "Connecting\xE2\x80\xA6"), cw);
    }
    return centred(b, dark, SC_STR_LIT(fa, "Run a query to see its results here."), cw);
}

/* ---- The tab strip (U12) ---- */

#define TAB_TITLE_BYTES 60u /* of a tab's first SQL line */

/* A tab's title: a dot while it streams, the first line of its SQL with an
 * ellipsis when there is more, " · waiting" after it while it queues, and
 * "Query" for blank SQL; then cut to `max_w` points. */
static sc_str tab_label(frame_builder *b, view_tab t, uint32_t max_w) {
    sc_arena *fa = b->arena;
    bool cut = false;
    sc_str title = t.arena != NULL ? tab_title(t.arena, t.sql, TAB_TITLE_BYTES, &cut)
                                   : SC_STR_LIT(fa, "");
    text_line l = text_new(fa, TAB_TITLE_BYTES + 64u, &b->failed);
    if (t.streaming) {
        TEXT_LIT(&l, "\xE2\x97\x8F "); /* ● */
    }
    if (title.len == 0) {
        TEXT_LIT(&l, "Query");
    } else {
        text_clean(&l, t.arena, title, TAB_TITLE_BYTES);
    }
    if (cut) {
        TEXT_LIT(&l, "\xE2\x80\xA6");
    }
    if (t.waiting) {
        TEXT_LIT(&l, " \xC2\xB7 waiting");
    }
    return fit(b, text_view(&l), UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, max_w);
}

static ui_node fixed_box(uint32_t w, uint32_t h) {
    ui_node n = frame_box();
    n.width.mode = UI_SIZE_FIXED;
    n.width.min = (uint16_t)w;
    n.height.mode = UI_SIZE_FIXED;
    n.height.min = (uint16_t)h;
    n.align_x = UI_ALIGN_CENTER;
    n.align_y = UI_ALIGN_CENTER;
    return n;
}

/* One tab: its close box and its title, with a rule at its right end. */
static bool strip_tab(frame_builder *b, const grid_view *v, size_t i, uint32_t w) {
    view_tab t;
    bool dark = v->dark;
    bool sel = i == v->tab_selected;
    uint32_t h = TAB_STRIP_HEIGHT - 1u;
    ui_node tab = fixed_box(w - 1u, h);
    tab.align_x = UI_ALIGN_START;
    tab.pad_left = TAB_CLOSE_INSET;
    tab.gap = 4;
    tab.tag = TAB_TAG_SELECT + (uint32_t)i;
    if (sel) { /* the selected tab joins the content below it */
        tab.background = theme_color(dark, THEME_CONTENT);
    }
    ui_node close = fixed_box(TAB_CLOSE_SIZE, TAB_CLOSE_SIZE);
    close.tag = TAB_TAG_CLOSE + (uint32_t)i;
    ui_node edge = fixed_box(1, h);
    edge.background = theme_color(dark, THEME_SIDEBAR_BORDER);
    bool has_close = w > TAB_CLOSE_INSET + TAB_CLOSE_SIZE;
    uint32_t used = TAB_CLOSE_INSET + (has_close ? TAB_CLOSE_SIZE + 4u : 0u) + 8u;
    bool ok = SC_TSLICE_GET(view_tab, v->tabs_arena, v->tabs, i, &t) && frame_open(b, tab);
    if (ok && has_close) {
        ok = frame_open(b, close) &&
             text(b, dark, SC_STR_LIT(b->arena, "\xC3\x97"), UI_FONT_SANS, THEME_TEXT_SIZE, /* × */
                  THEME_SECONDARY_TEXT) &&
             frame_close(b);
    }
    if (ok && w > used) {
        ok = text(b, dark, tab_label(b, t, w - used), sel ? UI_FONT_SANS_MEDIUM : UI_FONT_SANS,
                  THEME_SMALL_TEXT_SIZE, sel ? THEME_TEXT : THEME_SECONDARY_TEXT);
    }
    return ok && frame_close(b) && frame_leaf(b, edge);
}

/* shell_strip_fn: the tabs, each with its close box, and the +, over a rule.
 * The tabs share the content column's width, which is estimated from the
 * theme only to size them; clicks land on the rectangles the adapter reports. */
static bool tab_strip(frame_builder *b, const void *ctx) {
    const grid_view *v = ctx;
    bool dark = v->dark;
    uint32_t strip_w = content_width(v) + 2u; /* the content box and its borders */
    uint32_t w = tab_width(strip_w, v->tab_count);
    ui_node strip = frame_box();
    strip.width.mode = UI_SIZE_GROW;
    strip.direction = UI_COLUMN;
    strip.background = theme_color(dark, THEME_TOOLBAR);
    ui_node row = frame_box();
    row.width.mode = UI_SIZE_GROW;
    row.height.mode = UI_SIZE_FIXED;
    row.height.min = TAB_STRIP_HEIGHT - 1u;
    ui_node plus = fixed_box(TAB_PLUS_WIDTH, TAB_STRIP_HEIGHT - 1u);
    plus.tag = TAB_TAG_NEW;
    bool ok = frame_open(b, strip) && frame_open(b, row);
    for (size_t i = 0; ok && w > 1u && i < v->tab_count; i++) {
        ok = strip_tab(b, v, i, w);
    }
    if (ok && v->tab_count < TABS_MAX) {
        ok = frame_open(b, plus) &&
             text(b, dark, SC_STR_LIT(b->arena, "+"), UI_FONT_SANS, THEME_TEXT_SIZE + 2u,
                  THEME_SECONDARY_TEXT) &&
             frame_close(b);
    }
    return ok && frame_close(b) && rule(b, dark) && frame_close(b);
}

bool grid_build(frame_builder *b, const grid_view *v) {
    sc_arena *fa = b->arena;
    v->grid->grew = false;
    shell_state s;
    s.dark = v->dark;
    s.show_sidebar = true;
    s.object = SC_STR_LIT(fa, "Query");
    s.server = v->custom_conninfo ? SC_STR_LIT(fa, "--conninfo connection")
                                  : SC_STR_LIT(fa, "PG* environment");
    s.result_label = SC_STR_LIT(fa, "Query Result");
    if (v->res->cols > GRID_MAX_COLUMNS) { /* the grid draws the first ones only */
        text_line lab = text_new(fa, 64, &b->failed);
        TEXT_LIT(&lab, "Query Result \xC2\xB7 ");
        text_u64(&lab, GRID_MAX_COLUMNS, true);
        TEXT_LIT(&lab, " of ");
        text_u64(&lab, v->res->cols, true);
        TEXT_LIT(&lab, " columns");
        s.result_label = text_view(&lab);
    }
    s.placeholder = SC_STR_LIT(fa, "");
    s.strip = tab_strip;
    s.strip_ctx = v;
    const sc_arena *ta = terminal_arena(v);
    query_terminal t = query_terminal_read(ta);
    text_line e = text_new(fa, 32, &b->failed);
    if (v->ran) {
        text_ms(&e, v->elapsed_ms);
    }
    query_connection(fa, v->link->ui.state, v->link->broken, &s.connection, &s.connection_label);
    conn_ui shown = *v->ui;
    shown.state = v->link->ui.state;
    if (v->link->broken && conn_ui_active(&shown)) {
        shown.q = Q_DISCONNECTED; /* no reply will end it */
    }
    s.result_count = query_count(fa, &shown, ta, &t, v->elapsed_ms, &s.status, &b->failed);
    if (v->waiting) { /* also over a kept result, which stays on screen until the run starts */
        s.result_count = SC_STR_LIT(fa, "Waiting for connection\xE2\x80\xA6");
        s.status = SHELL_STATUS_NONE;
    }
    s.elapsed = text_view(&e);
    ui_node note = frame_box();
    note.pad_left = INPUT_PAD;
    note.pad_bottom = 4;
    note.height.mode = UI_SIZE_FIXED;
    note.height.min = NOTICE_HEIGHT;
    sc_str notice = query_notice(fa, v->notice);
    return shell_open(b, &s) && input_region(b, v) &&
           (notice.len == 0 ||
            (frame_open(b, note) &&
             fitted(b, v->dark, notice, UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, THEME_ERROR,
                    content_width(v) - INPUT_PAD) &&
             frame_close(b))) &&
           rule(b, v->dark) && result_area(b, v) && shell_close(b, &s);
}
