#include "view_editor.h"
#include "focus.h"
#include "theme.h"
#include "view_text.h"

#define TOP 4
#define GUTTER_GAP 10
#define LH VIEW_EDITOR_LINE_HEIGHT

typedef struct {
    uint32_t adv;
    int32_t text_x;
    size_t rows, cols;
} geometry;

static uint32_t digits(size_t n) {
    uint32_t d = 1;
    while (n >= 10u) {
        n /= 10u;
        d++;
    }
    return d;
}

static geometry layout_of(sc_arena *a, const editor *e, uint32_t width, uint32_t height) {
    geometry g;
    uint32_t ten = view_text_width(a, SC_STR_LIT(a, "MMMMMMMMMM"), UI_FONT_MONO, THEME_TEXT_SIZE);
    g.adv = ten >= 10u ? ten / 10u : 8u;
    uint32_t d = digits(e->lines);
    uint32_t text_x = VIEW_EDITOR_PAD + (d > 2u ? d : 2u) * g.adv + GUTTER_GAP;
    g.text_x = (int32_t)text_x;
    g.rows = height > 2u * TOP + LH ? (height - 2u * TOP) / LH : 1u;
    g.cols = width > text_x + VIEW_EDITOR_PAD + g.adv ? (width - text_x - VIEW_EDITOR_PAD) / g.adv : 1u;
    return g;
}

uint32_t view_editor_height(uint32_t centre) {
    uint32_t least = 4u * LH + 2u * TOP;
    uint32_t share = centre / 5u * 2u;
    return share > least ? share : least;
}

static uint32_t ink_token(uint8_t ink) {
    switch (ink) {
    case EDITOR_INK_KEYWORD:
        return THEME_KEYWORD;
    case EDITOR_INK_STRING:
        return THEME_STRING;
    case EDITOR_INK_COMMENT:
        return THEME_SECONDARY_TEXT;
    default:
        return THEME_TEXT;
    }
}

static int32_t column_x(const geometry *g, size_t col) {
    return g->text_x + (int32_t)(col * g->adv);
}

typedef struct {
    frame_builder *b;
    const editor_view *v;
    const geometry *g;
    size_t line_from;
    size_t caret;
    int32_t shift;
    int32_t y;
} line_ctx;

static bool run(const line_ctx *c, size_t from, size_t to, uint32_t token) {
    frame_builder *b = c->b;
    const editor *e = c->v->ed;
    if (from >= to) {
        return true;
    }
    sc_str raw = editor_copy(e, from, to, b->arena);
    sc_str shown = text_clean_copy(b->arena, b->arena, raw, raw.len, &b->failed);
    int32_t x = column_x(c->g, editor_columns(e, c->line_from, from)) + (from >= c->caret ? c->shift : 0);
    int32_t room = (int32_t)c->v->width - x;
    return room <= 0 || view_draw_text_in(b, shown, UI_FONT_MONO, THEME_TEXT_SIZE, x, c->y + 1, room, LH - 1,
                                          theme_color(c->v->dark, token));
}

static bool line_text(const line_ctx *c, size_t vis_to) {
    frame_builder *b = c->b;
    sc_tslice sp;
    size_t n = editor_spans(c->v->ed, c->v->kw, c->line_from, vis_to, b->arena, &sp);
    for (size_t i = 0; i < n; i++) {
        editor_span s;
        if (!SC_TSLICE_GET(editor_span, b->arena, sp, i, &s)) {
            return false;
        }
        uint32_t token = ink_token(s.ink);
        size_t cut = c->caret > s.from && c->caret < s.to ? c->caret : s.to;
        if (!run(c, s.from, cut, token) || !run(c, cut, s.to, token)) {
            return false;
        }
    }
    return true;
}

static bool number(frame_builder *b, const editor_view *v, const geometry *g, size_t line, int32_t y) {
    text_line l = text_new(b->arena, 24, &b->failed);
    text_u64(&l, (uint64_t)line + 1u, false);
    sc_str t = text_view(&l);
    int32_t w = (int32_t)view_text_width(b->arena, t, UI_FONT_MONO, THEME_TEXT_SIZE);
    return view_draw_text_in(b, t, UI_FONT_MONO, THEME_TEXT_SIZE, g->text_x - GUTTER_GAP - w, y + 1, 0, 0,
                             theme_color(v->dark, THEME_SECONDARY_TEXT));
}

static bool selection(frame_builder *b, const editor_view *v, const geometry *g, size_t ls, size_t le,
                      int32_t y) {
    const editor *e = v->ed;
    size_t from = 0, to = 0;
    editor_selection(e, &from, &to);
    size_t a = from > ls ? from : ls;
    size_t z = to < le ? to : le;
    if (from >= to || a > z || (a == z && to <= le)) {
        return true;
    }
    size_t left = e->left;
    size_t c0 = editor_columns(e, ls, a);
    size_t c1 = editor_columns(e, ls, z) + (to > le ? 1u : 0u);
    c0 = c0 > left ? c0 - left : 0u;
    c1 = c1 > left ? c1 - left : 0u;
    c1 = c1 < g->cols + 1u ? c1 : g->cols + 1u;
    uint32_t token = v->focused ? THEME_SIDEBAR_SELECTION : THEME_SIDEBAR_SELECTION_UNFOCUSED;
    return c1 <= c0 || frame_draw(b, view_item(UI_DRAW_RECT, column_x(g, c0), y, (int32_t)((c1 - c0) * g->adv),
                                              LH, theme_color(v->dark, token)));
}

static bool composition(const line_ctx *c, int32_t *caret_x) {
    frame_builder *b = c->b;
    const editor *e = c->v->ed;
    sc_str comp = editor_compose_text(e);
    sc_str shown = text_clean_copy(b->arena, e->arena, comp, comp.len, &b->failed);
    sc_str before = text_clean_copy(b->arena, e->arena, sc_slice_sub(e->arena, comp, 0, e->compose_caret),
                                    e->compose_caret, &b->failed);
    int32_t x = *caret_x;
    ui_color ink = theme_color(c->v->dark, THEME_TEXT);
    *caret_x = x + (int32_t)view_text_width(b->arena, before, UI_FONT_MONO, THEME_TEXT_SIZE);
    return view_draw_text_in(b, shown, UI_FONT_MONO, THEME_TEXT_SIZE, x, c->y + 1, 0, 0, ink) &&
           frame_draw(b, view_item(UI_DRAW_LINE, x, c->y + LH - 3, c->shift, 0, ink));
}

bool view_editor(frame_builder *b, const editor_view *v) {
    editor *e = v->ed;
    sc_arena *fa = b->arena;
    geometry g = layout_of(fa, e, v->width, v->height);
    editor_scroll(e, g.rows, g.cols);
    size_t len = editor_len(e);
    size_t band0 = 0, band1 = 0;
    bool band = editor_statement(e, &band0, &band1);
    sc_str comp = editor_compose_text(e);
    int32_t comp_w = comp.len > 0 ? (int32_t)view_text_width(fa, text_clean_copy(fa, e->arena, comp, comp.len,
                                                                                 &b->failed),
                                                               UI_FONT_MONO, THEME_TEXT_SIZE)
                                  : 0;
    bool ok = true;
    if (len == 0 && comp.len == 0) {
        ok = view_draw_text_in(b, SC_STR_LIT(fa, "Type SQL, then press \xE2\x8C\x98\xE2\x86\xA9 to run the statement at the cursor."),
                               UI_FONT_MONO, THEME_TEXT_SIZE, g.text_x, TOP + 1, 0, 0,
                               theme_color(v->dark, THEME_SECONDARY_TEXT));
    }
    size_t ls = e->top_off;
    for (size_t r = 0; ok && r < g.rows; r++) {
        size_t le = editor_line_end(e, ls);
        size_t line = e->top + r;
        int32_t y = TOP + (int32_t)(r * LH);
        bool caret_line = e->cursor >= ls && e->cursor <= le;
        size_t vis_from = editor_advance(e, ls, e->left);
        line_ctx c = {b, v, &g, vis_from, caret_line && comp.len > 0 ? e->cursor : SIZE_MAX, comp_w, y};
        ok = (!band || ls >= band1 || le < band0 ||
              frame_draw(b, view_item(UI_DRAW_RECT, g.text_x - 4, y, (int32_t)v->width - g.text_x + 4, LH,
                                      theme_color(v->dark, THEME_STATEMENT_BAND)))) &&
             number(b, v, &g, line, y) && selection(b, v, &g, ls, le, y) &&
             line_text(&c, editor_advance(e, vis_from, g.cols + 1u));
        if (ok && caret_line) {
            size_t col = editor_columns(e, ls, e->cursor);
            int32_t x = col >= e->left ? column_x(&g, col - e->left) : g.text_x;
            ok = (comp.len == 0 || composition(&c, &x)) &&
                 (!v->focused ||
                  frame_draw(b, view_item(UI_DRAW_LINE, x, y + 1, 0, LH - 2, theme_color(v->dark, THEME_ACCENT))));
        }
        if (le >= len) {
            break;
        }
        ls = le + 1u;
    }
    ui_node box = frame_box();
    box.width.mode = UI_SIZE_GROW;
    box.height.mode = UI_SIZE_FIXED;
    box.height.min = (uint16_t)v->height;
    box.tag = FOCUS_TAG_EDITOR;
    return ok && frame_custom(b, box);
}

size_t view_editor_hit(sc_arena *a, const editor *e, int32_t x, int32_t y) {
    geometry g = layout_of(a, e, UINT32_MAX / 2u, UINT32_MAX / 2u);
    size_t row = y > TOP ? (size_t)(y - TOP) / LH : 0u;
    size_t ls = editor_line_after_top(e, row);
    int32_t dx = x - g.text_x + (int32_t)(g.adv / 2u);
    size_t col = e->left + (dx > 0 ? (size_t)dx / g.adv : 0u);
    return editor_advance(e, ls, col);
}

bool view_editor_caret(sc_arena *a, const editor *e, uint32_t width, uint32_t height, ui_rect *line,
                       int32_t *cursor) {
    geometry g = layout_of(a, e, width, height);
    size_t at = 0, ls = e->top_off, le = editor_line_end(e, ls), len = editor_len(e);
    if (e->cursor < ls) {
        return false;
    }
    while (e->cursor > le) {
        if (++at >= g.rows || le >= len) {
            return false;
        }
        ls = le + 1u;
        le = editor_line_end(e, ls);
    }
    size_t col = editor_columns(e, ls, e->cursor);
    if (col < e->left) {
        return false;
    }
    line->tag = FOCUS_TAG_EDITOR;
    line->x = g.text_x;
    line->y = TOP + (int32_t)(at * LH);
    line->w = (int32_t)width - g.text_x;
    line->h = LH;
    *cursor = (int32_t)((col - e->left) * g.adv);
    return true;
}
