#include "picker.h"
#include "focus.h"
#include "query_text.h"
#include "theme.h"

static bool picker_label(frame_builder *b, bool dark, sc_str s, uint32_t color) {
    return frame_text(b, s, UI_FONT_SANS, THEME_TEXT_SIZE, theme_color(dark, color));
}

bool picker_build(frame_builder *b, const picker *p, bool dark, uint32_t width, uint32_t height,
                  const sc_arena *error_arena, sc_str error) {
    sc_arena *a = b->arena;
    ui_node root = frame_box();
    root.width.mode = root.height.mode = UI_SIZE_FIXED;
    root.width.min = (uint16_t)width;
    root.height.min = (uint16_t)height;
    root.direction = UI_COLUMN;
    root.align_x = root.align_y = UI_ALIGN_CENTER;
    root.background = theme_color(dark, THEME_WINDOW);
    ui_node card = frame_box();
    card.direction = UI_COLUMN;
    card.width.mode = UI_SIZE_FIXED;
    card.width.min = width > 660u ? 600u : (uint16_t)(width - 40u);
    card.gap = 12;
    card.pad_left = card.pad_right = card.pad_top = card.pad_bottom = 20;
    card.background = theme_color(dark, THEME_CONTENT);
    bool ok = frame_open(b, root) && frame_open(b, card);
    if (p->confirm) {
        text_line msg = text_new(a, 128, &b->failed);
        TEXT_LIT(&msg, "End session? Unsaved SQL in ");
        text_u64(&msg, p->unsaved, false);
        TEXT_LIT(&msg, " tabs will be lost");
        ok = ok && picker_label(b, dark, text_view(&msg), THEME_TEXT) &&
             shell_button(b, dark, SC_STR_LIT(a, "End Session"), PICK_TAG_END, SHELL_BUTTON_PRIMARY) &&
             shell_button(b, dark, SC_STR_LIT(a, "Cancel"), PICK_TAG_KEEP, SHELL_BUTTON_PLAIN);
    } else {
        ok = ok && frame_text(b, SC_STR_LIT(a, "Connect to PostgreSQL"), UI_FONT_SANS_MEDIUM, 20,
                              theme_color(dark, THEME_TEXT));
        size_t visible = height > 340u ? (height - 340u) / 30u : 2u;
        visible = visible > 12u ? 12u : visible;
        size_t first = p->selected >= visible ? p->selected - visible + 1u : 0;
        for (size_t i = first; ok && i <= p->cfg.count && i < first + visible; i++) {
            conncfg_entry e;
            bool env = i == p->cfg.count;
            if (!env && !conncfg_at(&p->cfg, i, &e)) return false;
            sc_str name = env ? SC_STR_LIT(a, "Environment (PG* and ~/.pgpass)")
                              : text_clean_copy(a, p->cfg.source, e.name, 100, &b->failed);
            ui_node row = frame_box();
            row.width.mode = UI_SIZE_GROW;
            row.height.mode = UI_SIZE_FIXED;
            row.height.min = 28;
            row.align_y = UI_ALIGN_CENTER;
            row.pad_left = 8;
            row.tag = PICK_TAG_ROW + (uint32_t)i;
            if (i == p->selected) row.background = theme_color(dark, THEME_ROW_SELECTION);
            ok = frame_open(b, row) && picker_label(b, dark, name,
                       i == p->selected ? THEME_SELECTED_TEXT : THEME_TEXT) && frame_close(b);
            if (!env && e.error != CFG_OK && i == p->selected) {
                text_line error = text_new(a, 200, &b->failed);
                TEXT_LIT(&error, "line ");
                text_u64(&error, e.line, false);
                TEXT_LIT(&error, ": ");
                sc_str message = conncfg_message(a, e.error);
                text_clean(&error, a, message, 128);
                ok = ok && picker_label(b, dark, text_view(&error), THEME_ERROR);
            }
        }
        if (p->file_error || p->cfg.error != CFG_OK) {
            ok = ok && picker_label(b, dark, p->file_error ? SC_STR_LIT(a, "Could not read connections.conf")
                                          : conncfg_message(a, p->cfg.error), THEME_ERROR);
        }
        ok = ok && picker_label(b, dark, text_clean_copy(a, &p->paths, p->path, 160, &b->failed),
                                 THEME_SECONDARY_TEXT);
        if (p->prompt) {
            ok = ok && picker_label(b, dark, p->failed ? SC_STR_LIT(a, "Authentication failed")
                                                      : SC_STR_LIT(a, "Password required"), THEME_ERROR);
            sc_buf bullets = sc_buf_new(sc_arena_alloc(a, 99));
            size_t count = sc_utf8_count(&p->secret, sc_buf_view(&p->secret, p->password));
            for (size_t i = 0; i < count && i < 30u; i++) (void)SC_BUF_LIT(a, &bullets, "\xE2\x80\xA2");
            ui_node field = frame_box();
            field.width.mode = UI_SIZE_GROW;
            field.height.mode = UI_SIZE_FIXED;
            field.height.min = 30;
            field.background = theme_color(dark, THEME_WINDOW);
            field.tag = FOCUS_TAG_PROMPT;
            ok = ok && frame_open(b, field) && picker_label(b, dark, sc_buf_view(a, bullets), THEME_TEXT) && frame_close(b);
        } else if (p->failed) {
            sc_str message = error.len > 0 ? text_clean_copy(a, error_arena, error, 90, &b->failed)
                                          : SC_STR_LIT(a, "Connection failed. Check the settings and try again.");
            ok = ok && picker_label(b, dark, message, THEME_ERROR);
        }
        ok = ok && shell_button(b, dark, p->connecting ? SC_STR_LIT(a, "Connecting\xE2\x80\xA6") : SC_STR_LIT(a, "Connect"),
                                PICK_TAG_CONNECT, p->connecting ? SHELL_BUTTON_DISABLED : SHELL_BUTTON_PRIMARY) &&
             shell_button(b, dark, SC_STR_LIT(a, "Cancel"), PICK_TAG_CANCEL, SHELL_BUTTON_PLAIN);
    }
    return ok && frame_close(b) && frame_close(b);
}
