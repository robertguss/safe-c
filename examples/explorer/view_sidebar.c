#include "view_sidebar.h"
#include "focus.h"
#include "query_text.h"
#include "view_text.h"

#define SIDE_HEADER 30u
#define SIDE_STATUS 26u
#define SIDE_RETRY 28u
#define SIDE_NAME_BYTES 128u
#define SIDE_LINE_BYTES 400u
#define SIDE_INDENT 16
#define SIDE_ICON_X 22
#define SIDE_ICON_W 14
#define SIDE_TEXT_GAP 6

static bool side_box(frame_builder *b, int32_t x, int32_t y, int32_t w, int32_t h, ui_color c) {
    return frame_draw(b, view_item(UI_DRAW_LINE, x, y, w, 0, c)) &&
           frame_draw(b, view_item(UI_DRAW_LINE, x, y + h - 1, w, 0, c)) &&
           frame_draw(b, view_item(UI_DRAW_LINE, x, y, 0, h, c)) &&
           frame_draw(b, view_item(UI_DRAW_LINE, x + w - 1, y, 0, h, c));
}

static bool side_grid(frame_builder *b, int32_t x, int32_t y, bool filled, ui_color c, ui_color bg) {
    int32_t w = SIDE_ICON_W, h = 12;
    ui_color line = filled ? bg : c;
    bool ok = filled ? frame_draw(b, view_item(UI_DRAW_RECT, x, y, w, h, c))
                     : side_box(b, x, y, w, h, c) &&
                           frame_draw(b, view_item(UI_DRAW_RECT, x, y, w, 3, c));
    return ok && frame_draw(b, view_item(UI_DRAW_LINE, x + 1, y + 3, w - 2, 0, line)) &&
           frame_draw(b, view_item(UI_DRAW_LINE, x + 1, y + 7, w - 2, 0, line)) &&
           frame_draw(b, view_item(UI_DRAW_LINE, x + 5, y + 4, 0, h - 5, line)) &&
           frame_draw(b, view_item(UI_DRAW_LINE, x + 9, y + 4, 0, h - 5, line));
}

static bool side_icon(frame_builder *b, const sidebar_view *v, tree_glyph g, int32_t x, int32_t y,
                      ui_color bg) {
    ui_color c = theme_color(v->dark, g.token);
    int32_t top = y + 6;
    switch (g.icon) {
    case TREE_ICON_FOLDER:
        return frame_draw(b, view_item(UI_DRAW_RECT, x, top - 1, 6, 2, c)) &&
               side_box(b, x, top + 1, SIDE_ICON_W + 1, 11, c);
    case TREE_ICON_TABLE:
    case TREE_ICON_MATVIEW:
    case TREE_ICON_FOREIGN:
        return side_grid(b, x, top, true, c, bg);
    case TREE_ICON_PARTITIONED:
        return side_box(b, x + 3, top - 3, SIDE_ICON_W, 12, c) && side_grid(b, x, top, true, c, bg);
    case TREE_ICON_VIEW:
        return side_grid(b, x, top, false, c, bg);
    case TREE_ICON_FUNCTION:
        return view_draw_text_in(b, SC_STR_LIT(b->arena, "\xC6\x92"), UI_FONT_SANS_MEDIUM, THEME_TEXT_SIZE, x + 3,
                                 y + 3, 0, THEME_SIDEBAR_ROW_HEIGHT - 4, c);
    default:
        return true;
    }
}

static bool side_chevron(frame_builder *b, bool open, int32_t y, ui_color c) {
    sc_str g = open ? SC_STR_LIT(b->arena, "\xE2\x8C\x84") : SC_STR_LIT(b->arena, "\xE2\x80\xBA");
    return view_draw_text_in(b, g, UI_FONT_SANS_MEDIUM, THEME_TEXT_SIZE + 2u, 8, open ? y - 1 : y + 2, 0, 0, c);
}

static bool side_row(frame_builder *b, const sidebar_view *v, tree_row r, int32_t y, int32_t w,
                     bool sel) {
    sc_arena *fa = b->arena;
    const catalog *c = v->cat;
    bool dark = v->dark;
    bool grey = v->status == SIDEBAR_DISCONNECTED;
    ui_color bg = theme_color(dark, THEME_CONTENT);
    if (sel) {
        bg = theme_color(dark, v->focused ? THEME_SIDEBAR_SELECTION : THEME_SIDEBAR_SELECTION_UNFOCUSED);
        if (!frame_draw(b, view_item(UI_DRAW_RECT, 4, y, w - 8, THEME_SIDEBAR_ROW_HEIGHT, bg))) {
            return false;
        }
    }
    ui_color ink = theme_color(dark, grey ? THEME_SECONDARY_TEXT : THEME_TEXT);
    ui_color muted = theme_color(dark, THEME_SECONDARY_TEXT);
    int32_t depth = r.kind == TREE_ROW_SCHEMA ? 0 : SIDE_INDENT;
    int32_t icon_x = SIDE_ICON_X + depth;
    int32_t text_x = icon_x + SIDE_ICON_W + SIDE_TEXT_GAP;
    catalog_schema s;
    if (!catalog_schema_at(c, r.schema, &s)) {
        return false;
    }
    sc_str name = SC_STR_LIT(fa, "");
    sc_str extra = SC_STR_LIT(fa, "");
    ui_color name_ink = ink;
    switch (r.kind) {
    case TREE_ROW_SCHEMA:
        name = text_clean_copy(fa, c->arena, s.name, SIDE_NAME_BYTES, &b->failed);
        if (!side_chevron(b, s.expanded, y, muted)) {
            return false;
        }
        break;
    case TREE_ROW_RELATION: {
        catalog_relation rel;
        if (!catalog_relation_at(c, &s, r.child, &rel)) {
            return false;
        }
        name = text_clean_copy(fa, c->arena, rel.name, SIDE_NAME_BYTES, &b->failed);
        break;
    }
    case TREE_ROW_FUNCTION: {
        catalog_function f;
        if (!catalog_function_at(c, &s, r.child, &f)) {
            return false;
        }
        name = text_clean_copy(fa, c->arena, f.name, SIDE_NAME_BYTES, &b->failed);
        text_line l = text_new(fa, SIDE_LINE_BYTES, &b->failed);
        TEXT_LIT(&l, " (");
        text_clean(&l, c->arena, f.args, SIDE_LINE_BYTES - 8u);
        TEXT_LIT(&l, ")");
        extra = text_view(&l);
        break;
    }
    case TREE_ROW_LOADING:
        text_x = icon_x;
        name = SC_STR_LIT(fa, "Loading\xE2\x80\xA6");
        name_ink = muted;
        break;
    case TREE_ROW_ERROR:
        text_x = icon_x;
        name = text_clean_copy(fa, c->arena, s.error, SIDE_LINE_BYTES, &b->failed);
        name_ink = theme_color(dark, THEME_ERROR);
        break;
    case TREE_ROW_RETRY:
        text_x = icon_x;
        name = SC_STR_LIT(fa, "\xE2\x86\xBB Retry");
        name_ink = theme_color(dark, THEME_ACCENT);
        break;
    default:
        return false;
    }
    uint32_t room = w > text_x + 8 ? (uint32_t)(w - text_x - 8) : 0u;
    name = view_fit(b, name, UI_FONT_SANS, THEME_TEXT_SIZE, room);
    uint32_t used = extra.len > 0 ? view_text_width(fa, name, UI_FONT_SANS, THEME_TEXT_SIZE) : room;
    bool ok = side_icon(b, v, tree_glyph_for(c, r), icon_x, y, bg) &&
              view_draw_text_in(b, name, UI_FONT_SANS, THEME_TEXT_SIZE, text_x, y + 3, (int32_t)room,
                                THEME_SIDEBAR_ROW_HEIGHT - 4, name_ink);
    if (ok && extra.len > 0 && used < room) {
        extra = view_fit(b, extra, UI_FONT_SANS, THEME_TEXT_SIZE, room - used);
        ok = view_draw_text_in(b, extra, UI_FONT_SANS, THEME_TEXT_SIZE, text_x + (int32_t)used, y + 3,
                               (int32_t)(room - used), THEME_SIDEBAR_ROW_HEIGHT - 4, muted);
    }
    return ok;
}

uint32_t sidebar_rows_height(const sidebar_view *v) {
    uint32_t status = v->status == SIDEBAR_READY ? 0u
                      : v->status == SIDEBAR_ERROR && v->retry != SHELL_RECONNECT_NONE ? SIDE_STATUS + SIDE_RETRY
                                                                                       : SIDE_STATUS;
    uint32_t chrome = THEME_TOOLBAR_HEIGHT + 1u + 2u * THEME_INSET + 2u + SIDE_HEADER + 1u + status;
    return v->height > chrome + THEME_SIDEBAR_ROW_HEIGHT ? v->height - chrome : THEME_SIDEBAR_ROW_HEIGHT;
}

static bool side_rows(frame_builder *b, const sidebar_view *v) {
    uint32_t h = sidebar_rows_height(v);
    int32_t w = THEME_SIDEBAR_WIDTH - 2;
    sc_tslice rows = SC_TSLICE_ALLOC(tree_row, b->arena, h / THEME_SIDEBAR_ROW_HEIGHT + 2u);
    grid_rows vis;
    size_t n = tree_visible(v->cat, v->tree, h, b->arena, rows, &vis);
    uint64_t sel = 0;
    bool has_sel = v->tree->selected && tree_index_of(v->cat, v->tree->mode, v->tree->sel, &sel);
    bool ok = true;
    for (size_t k = 0; ok && k < n; k++) {
        tree_row r;
        int32_t y = (int32_t)vis.top + (int32_t)k * THEME_SIDEBAR_ROW_HEIGHT;
        ok = SC_TSLICE_GET(tree_row, b->arena, rows, k, &r) &&
             side_row(b, v, r, y, w, has_sel && sel == vis.first + k);
    }
    ui_node region = frame_box();
    region.width.mode = UI_SIZE_GROW;
    region.height.mode = UI_SIZE_FIXED;
    region.height.min = (uint16_t)h;
    region.tag = FOCUS_TAG_SIDEBAR_ROWS;
    return ok && frame_custom(b, region);
}

static bool side_half(frame_builder *b, const sidebar_view *v, sc_str label, bool on, uint32_t tag) {
    ui_node n = frame_box();
    n.width.mode = UI_SIZE_GROW;
    n.height.mode = UI_SIZE_FIXED;
    n.height.min = THEME_BUTTON_HEIGHT - 2u;
    n.align_x = UI_ALIGN_CENTER;
    n.align_y = UI_ALIGN_CENTER;
    n.radius = THEME_BUTTON_RADIUS - 1u;
    n.background = theme_color(v->dark, on ? THEME_ACCENT : THEME_CONTENT);
    n.tag = tag;
    return frame_open(b, n) &&
           frame_text(b, label, UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE,
                      theme_color(v->dark, on ? THEME_SELECTED_TEXT : THEME_TEXT)) &&
           frame_close(b);
}

static bool side_header(frame_builder *b, const sidebar_view *v) {
    sc_arena *fa = b->arena;
    ui_node bar = frame_box();
    bar.width.mode = UI_SIZE_GROW;
    bar.height.mode = UI_SIZE_FIXED;
    bar.height.min = SIDE_HEADER;
    bar.align_y = UI_ALIGN_CENTER;
    bar.pad_left = 6;
    bar.pad_right = 6;
    bar.gap = 6;
    ui_node sw = frame_box();
    sw.width.mode = UI_SIZE_GROW;
    sw.height.mode = UI_SIZE_FIXED;
    sw.height.min = THEME_BUTTON_HEIGHT;
    sw.pad_left = 1;
    sw.pad_right = 1;
    sw.pad_top = 1;
    sw.pad_bottom = 1;
    sw.radius = THEME_BUTTON_RADIUS;
    sw.border = 1;
    sw.border_color = theme_color(v->dark, THEME_SIDEBAR_BORDER);
    bool tables = v->tree == NULL || v->tree->mode == TREE_TABLES;
    return frame_open(b, bar) && frame_open(b, sw) &&
           side_half(b, v, SC_STR_LIT(fa, "Tables"), tables, FOCUS_TAG_SIDEBAR_TABLES) &&
           side_half(b, v, SC_STR_LIT(fa, "Functions"), !tables, FOCUS_TAG_SIDEBAR_FUNCTIONS) &&
           frame_close(b) &&
           shell_button(b, v->dark, SC_STR_LIT(fa, "\xE2\x86\xBB"), FOCUS_TAG_SIDEBAR_RELOAD,
                        v->can_reload ? SHELL_BUTTON_PLAIN : SHELL_BUTTON_DISABLED) &&
           frame_close(b);
}

static bool side_status(frame_builder *b, const sidebar_view *v) {
    sc_arena *fa = b->arena;
    sc_str line = v->status == SIDEBAR_CONNECTING     ? SC_STR_LIT(fa, "Connecting\xE2\x80\xA6")
                  : v->status == SIDEBAR_LOADING      ? SC_STR_LIT(fa, "Loading\xE2\x80\xA6")
                  : v->status == SIDEBAR_DISCONNECTED ? SC_STR_LIT(fa, "Disconnected")
                  : v->status == SIDEBAR_CAPTION ? SC_STR_LIT(fa, "Tables and views will appear here.")
                                                 : v->message;
    bool error = v->status == SIDEBAR_ERROR;
    ui_node row = frame_box();
    row.width.mode = UI_SIZE_GROW;
    row.height.mode = UI_SIZE_FIXED;
    row.height.min = SIDE_STATUS;
    row.align_y = UI_ALIGN_CENTER;
    row.pad_left = 10;
    row.pad_right = 8;
    bool ok = frame_open(b, row) &&
              frame_text(b, view_fit(b, line, UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, THEME_SIDEBAR_WIDTH - 20u),
                         UI_FONT_SANS, THEME_SMALL_TEXT_SIZE,
                         theme_color(v->dark, error ? THEME_ERROR : THEME_SECONDARY_TEXT)) &&
              frame_close(b);
    if (ok && error && v->retry != SHELL_RECONNECT_NONE) {
        ui_node r = frame_box();
        r.width.mode = UI_SIZE_GROW;
        r.height.mode = UI_SIZE_FIXED;
        r.height.min = SIDE_RETRY;
        r.align_y = UI_ALIGN_CENTER;
        r.pad_left = 10;
        ok = frame_open(b, r) &&
             shell_button(b, v->dark, v->retry_label, FOCUS_TAG_META_RETRY,
                          v->retry == SHELL_RECONNECT_ENABLED ? SHELL_BUTTON_PLAIN : SHELL_BUTTON_DISABLED) &&
             frame_close(b);
    }
    return ok;
}

bool sidebar_build(frame_builder *b, const void *ctx) {
    const sidebar_view *v = ctx;
    ui_node col = frame_box();
    col.width.mode = UI_SIZE_FIXED;
    col.width.min = THEME_SIDEBAR_WIDTH;
    col.height.mode = UI_SIZE_GROW;
    col.direction = UI_COLUMN;
    col.tag = FOCUS_TAG_SIDEBAR;
    ui_node box = frame_box();
    box.width.mode = UI_SIZE_GROW;
    box.height.mode = UI_SIZE_GROW;
    box.direction = UI_COLUMN;
    box.pad_left = 1;
    box.pad_right = 1;
    box.pad_top = 1;
    box.pad_bottom = 1;
    box.radius = THEME_BOX_RADIUS;
    box.border = 1;
    box.border_color = theme_color(v->dark, THEME_SIDEBAR_BORDER);
    box.background = theme_color(v->dark, THEME_CONTENT);
    ui_node rule = frame_box();
    rule.width.mode = UI_SIZE_GROW;
    rule.height.mode = UI_SIZE_FIXED;
    rule.height.min = 1;
    rule.background = theme_color(v->dark, THEME_SIDEBAR_BORDER);
    bool rows = v->cat != NULL && v->tree != NULL &&
                (v->status == SIDEBAR_READY || v->status == SIDEBAR_ERROR ||
                 v->status == SIDEBAR_DISCONNECTED);
    return frame_open(b, col) && frame_open(b, box) && side_header(b, v) && frame_leaf(b, rule) &&
           (v->status == SIDEBAR_READY || side_status(b, v)) && (!rows || side_rows(b, v)) &&
           frame_close(b) && frame_close(b);
}
