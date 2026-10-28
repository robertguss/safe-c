#include "view_table.h"
#include "focus.h"
#include "theme.h"
#include "view_text.h"

#define TV_NOTE_HEIGHT 24u
#define TV_NAME_BYTES 96u
#define TV_TEXT_BYTES 400u
#define TV_NAME_X 12
#define TV_TYPE_X 220
#define TV_CHIPS_X 400
#define TV_CHIP_H 15
#define TV_SWITCH_W 160u

static bool tv_unordered(const table_view *t) {
    return t->entry->view == NAV_CONTENT && t->found && page_unordered(t->g->cat, &t->rel);
}

/* The points a table view takes above its rows: the note, when shown. */
static uint32_t tv_chrome(const table_view *t) {
    return tv_unordered(t) ? TV_NOTE_HEIGHT + 1u : 0u;
}

static sc_str tv_title(frame_builder *b, const table_view *t) {
    if (!t->found) {
        return SC_STR_LIT(b->arena, "Table");
    }
    text_line l = text_new(b->arena, 2u * TV_NAME_BYTES + 16u, &b->failed);
    text_clean(&l, t->g->cat->arena, t->schema.name, TV_NAME_BYTES);
    TEXT_LIT(&l, ".");
    text_clean(&l, t->g->cat->arena, t->rel.name, TV_NAME_BYTES);
    return text_view(&l);
}

static bool tv_centred(frame_builder *b, const grid_view *v, sc_str t, uint32_t token) {
    ui_node box = frame_box();
    box.width.mode = UI_SIZE_GROW;
    box.height.mode = UI_SIZE_GROW;
    box.align_x = UI_ALIGN_CENTER;
    box.align_y = UI_ALIGN_CENTER;
    return frame_open(b, box) &&
           frame_text(b, view_fit(b, t, UI_FONT_SANS, THEME_TEXT_SIZE, grid_content_width(v) - 24u), UI_FONT_SANS,
                      THEME_TEXT_SIZE, theme_color(v->dark, token)) &&
           frame_close(b);
}

static bool tv_note(frame_builder *b, const grid_view *v) {
    ui_node row = frame_box();
    row.width.mode = UI_SIZE_GROW;
    row.height.mode = UI_SIZE_FIXED;
    row.height.min = TV_NOTE_HEIGHT;
    row.align_y = UI_ALIGN_CENTER;
    row.pad_left = 10;
    sc_str t = SC_STR_LIT(b->arena, "This relation has no row order, so rows may change between pages.");
    return frame_open(b, row) &&
           frame_text(b, view_fit(b, t, UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, grid_content_width(v) - 20u), UI_FONT_SANS,
                      THEME_SMALL_TEXT_SIZE, theme_color(v->dark, THEME_SECONDARY_TEXT)) &&
           frame_close(b) && grid_rule(b, v->dark);
}


static bool tv_chip(frame_builder *b, const grid_view *v, sc_str label, bool key, int32_t *x, int32_t y,
                    int32_t right) {
    sc_arena *fa = b->arena;
    int32_t w = (int32_t)view_text_width(fa, label, UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE) + 10;
    if (*x + w > right) {
        return true;
    }
    bool ok = frame_draw(b, view_item(UI_DRAW_RECT, *x, y + 4, w, TV_CHIP_H,
                                      theme_color(v->dark, key ? THEME_ACCENT : THEME_GRID_HEADER))) &&
              view_draw_text_in(b, label, UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE, *x + 5, y + 5, w - 5,
                                TV_CHIP_H, theme_color(v->dark, key ? THEME_SELECTED_TEXT : THEME_SECONDARY_TEXT));
    *x += w + 4;
    return ok;
}

static bool tv_text(frame_builder *b, const grid_view *v, const sc_arena *sa, sc_str s, uint8_t font,
                    int32_t x, int32_t y, int32_t w, uint32_t token) {
    if (w <= 8) {
        return true;
    }
    sc_str shown = view_fit(b, text_clean_copy(b->arena, sa, s, TV_TEXT_BYTES, &b->failed), font,
                            THEME_TEXT_SIZE, (uint32_t)w - 8u);
    return view_draw_text_in(b, shown, font, THEME_TEXT_SIZE, x, y + 4, w, (int32_t)TABLE_STRUCTURE_ROW - 4,
                             theme_color(v->dark, token));
}

static bool tv_column_chips(frame_builder *b, const grid_view *v, const page_row *p, int32_t y, int32_t w) {
    sc_arena *fa = b->arena;
    int32_t x = TV_CHIPS_X;
    bool ok = true;
    if (ok && (p->chips & PAGE_CHIP_PRIMARY_KEY) != 0) {
        ok = tv_chip(b, v, SC_STR_LIT(fa, "PRIMARY KEY"), true, &x, y, w);
    }
    if (ok && (p->chips & PAGE_CHIP_NOT_NULL) != 0) {
        ok = tv_chip(b, v, SC_STR_LIT(fa, "NOT NULL"), false, &x, y, w);
    }
    if (ok && (p->chips & PAGE_CHIP_UNIQUE) != 0) {
        ok = tv_chip(b, v, SC_STR_LIT(fa, "UNIQUE"), false, &x, y, w);
    }
    if (ok && (p->chips & PAGE_CHIP_FOREIGN) != 0) {
        text_line l = text_new(fa, TV_NAME_BYTES + 8u, &b->failed);
        TEXT_LIT(&l, "\xE2\x86\x92 ");
        text_clean(&l, v->cat->arena, p->target, TV_NAME_BYTES);
        ok = tv_chip(b, v, text_view(&l), false, &x, y, w);
    }
    if (ok && (p->chips & PAGE_CHIP_IDENTITY) != 0) {
        ok = tv_chip(b, v, SC_STR_LIT(fa, "IDENTITY"), false, &x, y, w);
    }
    if (ok && (p->chips & PAGE_CHIP_GENERATED) != 0) {
        sc_str label = (p->chips & PAGE_CHIP_VIRTUAL) != 0
            ? SC_STR_LIT(fa, "GENERATED VIRTUAL") : SC_STR_LIT(fa, "GENERATED");
        ok = tv_chip(b, v, label, false, &x, y, w);
    }
    if (ok && (p->chips & PAGE_CHIP_HIDDEN) != 0) {
        ok = tv_chip(b, v, SC_STR_LIT(fa, "HIDDEN"), false, &x, y, w);
    }
    if (ok && p->detail.len > 0) {
        text_line l = text_new(fa, TV_TEXT_BYTES + 16u, &b->failed);
        if ((p->chips & PAGE_CHIP_GENERATED) != 0) {
            TEXT_LIT(&l, "AS ");
        } else {
            TEXT_LIT(&l, "DEFAULT ");
        }
        text_clean(&l, v->cat->arena, p->detail, TV_TEXT_BYTES);
        ok = tv_text(b, v, fa, text_view(&l), UI_FONT_MONO, x + 4, y, w - x - 4, THEME_SECONDARY_TEXT);
    }
    return ok;
}

static bool tv_row(frame_builder *b, const grid_view *v, const page_row *p, int32_t y, int32_t w) {
    const sc_arena *ca = v->cat->arena;
    bool dark = v->dark;
    int32_t row_h = (int32_t)TABLE_STRUCTURE_ROW;
    if (p->kind == PAGE_ROW_SECTION) {
        return frame_draw(b, view_item(UI_DRAW_RECT, 0, y, w, row_h, theme_color(dark, THEME_GRID_HEADER))) &&
               view_draw_text_in(b, p->name, UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE, TV_NAME_X, y + 5, 0, 0,
                                 theme_color(dark, THEME_SECONDARY_TEXT)) &&
               frame_draw(b, view_item(UI_DRAW_LINE, 0, y + row_h - 1, w, 0, theme_color(dark, THEME_HEADER_LINE)));
    }
    bool ok = tv_text(b, v, ca, p->name, UI_FONT_SANS_MEDIUM, TV_NAME_X, y, TV_TYPE_X - TV_NAME_X, THEME_TEXT);
    if (p->kind == PAGE_ROW_COLUMN) {
        ok = ok && tv_text(b, v, ca, p->type, UI_FONT_SANS, TV_TYPE_X, y, TV_CHIPS_X - TV_TYPE_X,
                           THEME_SECONDARY_TEXT) &&
             tv_column_chips(b, v, p, y, w);
    } else {
        ok = ok && tv_text(b, v, ca, p->detail, UI_FONT_MONO, TV_TYPE_X, y, w - TV_TYPE_X, THEME_TEXT);
    }
    return ok && frame_draw(b, view_item(UI_DRAW_LINE, 0, y + row_h - 1, w, 0, theme_color(dark, THEME_COLUMN_LINE)));
}

static bool tv_structure(frame_builder *b, const grid_view *v, const catalog_relation *r) {
    grid_state *g = v->grid;
    uint32_t h = grid_rows_height(v, 0, 0);
    int32_t w = (int32_t)grid_content_width(v);
    uint64_t n = page_structure_count(v->cat, r);
    uint64_t total = n * TABLE_STRUCTURE_ROW;
    uint64_t max = total > h ? total - h : 0u;
    g->structure_y = g->structure_y < max ? g->structure_y : max;
    uint64_t first = g->structure_y / TABLE_STRUCTURE_ROW;
    int32_t top = -(int32_t)(g->structure_y % TABLE_STRUCTURE_ROW);
    bool ok = true;
    for (uint64_t k = 0; ok && first + k < n && (int64_t)top + (int64_t)k * (int64_t)TABLE_STRUCTURE_ROW < (int64_t)h; k++) {
        page_row p;
        ok = page_structure_row(v->cat, r, (size_t)(first + k), b->arena, &p) &&
             tv_row(b, v, &p, top + (int32_t)k * (int32_t)TABLE_STRUCTURE_ROW, w);
    }
    ui_node region = frame_box();
    region.width.mode = UI_SIZE_GROW;
    region.height.mode = UI_SIZE_FIXED;
    region.height.min = (uint16_t)h;
    region.background = theme_color(v->dark, THEME_CONTENT);
    region.tag = FOCUS_TAG_STRUCTURE;
    return ok && frame_custom(b, region);
}

static bool tv_link_down(const grid_view *v) {
    ui_conn_state st = v->conn_state;
    return v->link->broken || st == UI_CONN_DISCONNECTED || st == UI_CONN_AUTH_FAILED ||
           st == UI_CONN_UNREACHABLE;
}

static bool tv_centre(frame_builder *b, const table_view *t) {
    sc_arena *fa = b->arena;
    const grid_view *v = t->g;
    const catalog_relation *r = &t->rel;
    if (t->gone) {
        return tv_centred(b, v, SC_STR_LIT(fa, "This table is no longer in the database."), THEME_ERROR);
    }
    if (t->entry->view == NAV_CONTENT) {
        bool idle = !v->ran && !v->waiting && v->ui->q == Q_NONE;
        bool refused = idle && v->notice != QUERY_NOTICE_NONE;
        if (tv_unordered(t) && !tv_note(b, v)) {
            return false;
        }
        if (refused && !tv_link_down(v)) {
            return tv_centred(b, v, query_notice(fa, v->notice), THEME_ERROR);
        }
        return idle && !refused ? tv_centred(b, v, SC_STR_LIT(fa, "Loading\xE2\x80\xA6"), THEME_SECONDARY_TEXT)
                                : grid_result_area(b, v, tv_chrome(t));
    }
    if (!t->found || r->detail == CATALOG_LOAD_NONE || r->detail == CATALOG_LOAD_PENDING) {
        return tv_centred(b, v, SC_STR_LIT(fa, "Loading\xE2\x80\xA6"), THEME_SECONDARY_TEXT);
    }
    if (r->detail == CATALOG_LOAD_FAILED) {
        return tv_centred(b, v, text_clean_copy(fa, v->cat->arena, r->error, TV_TEXT_BYTES, &b->failed),
                          THEME_ERROR);
    }
    return tv_structure(b, v, r);
}


static bool tv_half(frame_builder *b, bool dark, sc_str label, bool on, uint32_t tag) {
    ui_node n = frame_box();
    n.width.mode = UI_SIZE_FIXED;
    n.width.min = TV_SWITCH_W / 2u - 1u;
    n.height.mode = UI_SIZE_FIXED;
    n.height.min = THEME_BUTTON_HEIGHT - 2u;
    n.align_x = UI_ALIGN_CENTER;
    n.align_y = UI_ALIGN_CENTER;
    n.radius = THEME_BUTTON_RADIUS - 1u;
    n.background = theme_color(dark, on ? THEME_ACCENT : THEME_CONTENT);
    n.tag = tag;
    return frame_open(b, n) &&
           frame_text(b, label, UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE,
                      theme_color(dark, on ? THEME_SELECTED_TEXT : THEME_TEXT)) &&
           frame_close(b);
}

static bool tv_switch(frame_builder *b, const table_view *t) {
    const grid_view *v = t->g;
    ui_node sw = frame_box();
    sw.height.mode = UI_SIZE_FIXED;
    sw.height.min = THEME_BUTTON_HEIGHT;
    sw.pad_left = 1;
    sw.pad_right = 1;
    sw.pad_top = 1;
    sw.pad_bottom = 1;
    sw.radius = THEME_BUTTON_RADIUS;
    sw.border = 1;
    sw.border_color = theme_color(v->dark, THEME_SIDEBAR_BORDER);
    sw.background = theme_color(v->dark, THEME_CONTENT);
    bool content = t->entry->view == NAV_CONTENT;
    return frame_open(b, sw) &&
           tv_half(b, v->dark, SC_STR_LIT(b->arena, "Content"), content, FOCUS_TAG_TABLE_CONTENT) &&
           tv_half(b, v->dark, SC_STR_LIT(b->arena, "Structure"), !content, FOCUS_TAG_TABLE_STRUCTURE) &&
           frame_close(b);
}

static bool tv_capped(const conn_ui *u) {
    return u->q == Q_CAPPED && u->cap != PROTO_CAP_LIMIT;
}

static sc_str tv_count(frame_builder *b, const table_view *t) {
    sc_arena *fa = b->arena;
    const grid_view *v = t->g;
    const conn_ui *u = v->ui;
    if (t->entry->view == NAV_STRUCTURE) {
        if (!t->found || t->rel.detail != CATALOG_LOAD_DONE) {
            return SC_STR_LIT(fa, "");
        }
        size_t cols = sc_tslice_len(t->rel.columns);
        text_line l = text_new(fa, 48, &b->failed);
        text_u64(&l, cols, true);
        if (cols == 1u) {
            TEXT_LIT(&l, " column");
        } else {
            TEXT_LIT(&l, " columns");
        }
        return text_view(&l);
    }
    if (v->waiting) {
        return SC_STR_LIT(fa, "Waiting for connection\xE2\x80\xA6");
    }
    if (t->gone) {
        return SC_STR_LIT(fa, "");
    }
    if (!v->ran) {
        return v->notice != QUERY_NOTICE_NONE ? query_notice(fa, v->notice) : SC_STR_LIT(fa, "Loading\xE2\x80\xA6");
    }
    if (!conn_ui_active(u) && (u->q == Q_ROWS || tv_capped(u))) {
        return page_rows_label(fa, t->entry->page, u->rows, t->entry->total, tv_capped(u), &b->failed);
    }
    const sc_arena *ta = u->terminal_kept ? link_arena(v->link, u->terminal) : NULL;
    query_terminal term = query_terminal_read(ta);
    uint8_t status = SHELL_STATUS_NONE;
    return query_count(fa, u, ta, &term, v->elapsed_ms, &status, &b->failed);
}

static bool tv_pager(frame_builder *b, const table_view *t) {
    sc_arena *fa = b->arena;
    bool dark = t->g->dark;
    return shell_button(b, dark, SC_STR_LIT(fa, "\xE2\x80\xB9"), FOCUS_TAG_PAGE_PREV,
                        t->can_prev ? SHELL_BUTTON_PLAIN : SHELL_BUTTON_DISABLED) &&
           frame_text(b, page_label(fa, t->entry->page, t->entry->total, &b->failed), UI_FONT_SANS,
                      THEME_SMALL_TEXT_SIZE, theme_color(dark, THEME_TEXT)) &&
           shell_button(b, dark, SC_STR_LIT(fa, "\xE2\x80\xBA"), FOCUS_TAG_PAGE_NEXT,
                        t->can_next ? SHELL_BUTTON_PLAIN : SHELL_BUTTON_DISABLED);
}

static bool tv_bottom(frame_builder *b, const void *ctx) {
    const table_view *t = ctx;
    const grid_view *v = t->g;
    bool dark = v->dark;
    ui_node bar = frame_box();
    bar.width.mode = UI_SIZE_GROW;
    bar.height.mode = UI_SIZE_FIXED;
    bar.height.min = THEME_BOTTOM_BAR_HEIGHT;
    bar.align_y = UI_ALIGN_CENTER;
    bar.pad_left = 10;
    bar.pad_right = 10;
    bar.gap = 8;
    bar.background = theme_color(dark, THEME_BOTTOM_BAR);
    ui_node third = frame_box();
    third.width.mode = UI_SIZE_GROW;
    third.height.mode = UI_SIZE_GROW;
    third.align_y = UI_ALIGN_CENTER;
    third.gap = 6;
    ui_node centre = third;
    centre.align_x = UI_ALIGN_CENTER;
    ui_node right = third;
    right.align_x = UI_ALIGN_END;
    uint32_t room = grid_content_width(v) / 3u;
    return grid_rule(b, dark) && frame_open(b, bar) && frame_open(b, third) && tv_switch(b, t) &&
           frame_close(b) && frame_open(b, centre) &&
           frame_text(b, view_fit(b, tv_count(b, t), UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, room), UI_FONT_SANS,
                      THEME_SMALL_TEXT_SIZE, theme_color(dark, THEME_TEXT)) &&
           frame_close(b) && frame_open(b, right) &&
           (t->entry->view != NAV_CONTENT || tv_pager(b, t)) &&
           (t->entry->view != NAV_STRUCTURE || v->cat->engine == SC_DB_POSTGRESQL ||
            shell_button(b, dark, SC_STR_LIT(b->arena, "Definition"), FOCUS_TAG_DEFINITION,
                         t->found && !t->gone ? SHELL_BUTTON_PLAIN : SHELL_BUTTON_DISABLED)) &&
           frame_close(b) && frame_close(b);
}

bool table_build(frame_builder *b, const table_view *t) {
    shell_state s;
    grid_shell(b, t->g, &s);
    s.object = tv_title(b, t);
    s.bottom = tv_bottom;
    s.bottom_ctx = t;
    return shell_open(b, &s) && tv_centre(b, t) && shell_close(b, &s);
}
