#include "view_history.h"
#include "query_text.h"
#include "theme.h"

bool history_build(frame_builder *b, const history *h, size_t selected, bool failed, bool dark, uint32_t height) {
    sc_arena *a = b->arena;
    ui_node box = frame_box();
    box.width.mode = UI_SIZE_GROW;
    box.height.mode = UI_SIZE_FIXED;
    box.height.min = (uint16_t)height;
    box.direction = UI_COLUMN;
    box.pad_left = box.pad_right = 8;
    box.gap = 3;
    bool ok = frame_open(b, box) && frame_text(b, SC_STR_LIT(a, "Query History  ·  Return to load  ·  Esc to close"),
                       UI_FONT_SANS_MEDIUM, THEME_TEXT_SIZE, theme_color(dark, THEME_SECONDARY_TEXT));
    if (h->damaged || failed) {
        ok = ok && frame_text(b, failed ? SC_STR_LIT(a, "Could not save history; it remains in memory")
                                       : SC_STR_LIT(a, "History damaged: recovered entries before the first bad record"),
                              UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, theme_color(dark, THEME_ERROR));
    }
    if (h->count == 0) {
        ok = ok && frame_text(b, SC_STR_LIT(a, "No query history yet"), UI_FONT_SANS, THEME_TEXT_SIZE,
                               theme_color(dark, THEME_SECONDARY_TEXT));
    }
    size_t visible = height > 70u ? (height - 70u) / 42u : 1u;
    size_t first = selected >= visible ? selected - visible + 1u : 0;
    for (size_t i = first; ok && i < h->count && i < first + visible; i++) {
        history_entry e;
        if (!history_get(h, h->count - i - 1u, &e)) return false;
        ui_node row = frame_box();
        row.width.mode = UI_SIZE_GROW;
        row.height.mode = UI_SIZE_FIXED;
        row.height.min = 39;
        row.direction = UI_COLUMN;
        row.tag = HISTORY_TAG_ROW + (uint32_t)i;
        row.pad_left = 6;
        if (i == selected) row.background = theme_color(dark, THEME_SIDEBAR_SELECTION);
        uint32_t ink = i == selected ? THEME_SELECTED_TEXT : THEME_TEXT;
        text_line label = text_new(a, 200, &b->failed);
        text_clean(&label, h->arena, e.name, 120);
        TEXT_LIT(&label, "  ·  ");
        text_u64(&label, e.time, false);
        ok = frame_open(b, row) &&
             frame_text(b, text_view(&label), UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, theme_color(dark, ink)) &&
             frame_text(b, text_clean_copy(a, h->arena, e.sql, 90, &b->failed), UI_FONT_MONO, THEME_TEXT_SIZE,
                        theme_color(dark, ink)) && frame_close(b);
    }
    return ok && frame_close(b);
}
