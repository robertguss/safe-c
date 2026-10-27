#include "view_shell.h"
#include "focus.h"
#include "theme.h"

static ui_node pad(ui_node n, uint16_t h, uint16_t v) {
    n.pad_left = h;
    n.pad_right = h;
    n.pad_top = v;
    n.pad_bottom = v;
    return n;
}

static ui_node shell_grow(uint8_t direction) {
    ui_node n = frame_box();
    n.width.mode = UI_SIZE_GROW;
    n.height.mode = UI_SIZE_GROW;
    n.direction = direction;
    return n;
}

static ui_node fixed_height(uint16_t h) {
    ui_node n = frame_box();
    n.width.mode = UI_SIZE_GROW;
    n.height.mode = UI_SIZE_FIXED;
    n.height.min = h;
    n.align_y = UI_ALIGN_CENTER;
    return n;
}

static bool shell_text(frame_builder *b, const shell_state *s, sc_str t, uint8_t font, uint16_t size,
                 uint32_t token) {
    return frame_text(b, t, font, size, theme_color(s->dark, token));
}

/* A 1-point rule across the parent. */
static bool shell_rule(frame_builder *b, const shell_state *s) {
    ui_node n = fixed_height(1);
    n.background = theme_color(s->dark, THEME_SIDEBAR_BORDER);
    return frame_leaf(b, n);
}

/* A toolbar button with one glyph. */
static bool nav_button(frame_builder *b, const shell_state *s, sc_str glyph) {
    ui_node n = frame_box();
    n.width.mode = UI_SIZE_FIXED;
    n.width.min = 26;
    n.height.mode = UI_SIZE_FIXED;
    n.height.min = THEME_BUTTON_HEIGHT;
    n.align_x = UI_ALIGN_CENTER;
    n.align_y = UI_ALIGN_CENTER;
    n.radius = THEME_BUTTON_RADIUS;
    n.border = 1;
    n.border_color = theme_color(s->dark, THEME_SIDEBAR_BORDER);
    n.background = theme_color(s->dark, THEME_CONTENT);
    return frame_open(b, n) &&
           shell_text(b, s, glyph, UI_FONT_SANS_MEDIUM, THEME_TEXT_SIZE, THEME_SECONDARY_TEXT) &&
           frame_close(b);
}

bool shell_button(frame_builder *b, bool dark, sc_str label, uint32_t tag, uint8_t style) {
    ui_node n = pad(frame_box(), 10, 0);
    n.height.mode = UI_SIZE_FIXED;
    n.height.min = THEME_BUTTON_HEIGHT;
    n.align_x = UI_ALIGN_CENTER;
    n.align_y = UI_ALIGN_CENTER;
    n.radius = THEME_BUTTON_RADIUS;
    bool primary = style == SHELL_BUTTON_PRIMARY;
    n.border = primary ? 0 : 1;
    n.border_color = theme_color(dark, THEME_SIDEBAR_BORDER);
    n.background = theme_color(dark, primary ? THEME_ACCENT : THEME_CONTENT);
    n.tag = style == SHELL_BUTTON_DISABLED ? 0u : tag;
    uint32_t ink = primary                         ? THEME_SELECTED_TEXT
                   : style == SHELL_BUTTON_DISABLED ? THEME_SECONDARY_TEXT
                                                    : THEME_TEXT;
    return frame_open(b, n) &&
           frame_text(b, label, UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE, theme_color(dark, ink)) &&
           frame_close(b);
}

static uint32_t connection_token(uint8_t c) {
    return c == SHELL_CONNECTED    ? THEME_SUCCESS
           : c == SHELL_CONNECTING ? THEME_ACCENT
           : c == SHELL_FAILED     ? THEME_ERROR
                                   : THEME_SECONDARY_TEXT;
}

static bool toolbar(frame_builder *b, const shell_state *s) {
    ui_node bar = pad(fixed_height(THEME_TOOLBAR_HEIGHT), 10, 0);
    bar.gap = 6;
    bar.background = theme_color(s->dark, THEME_TOOLBAR);
    ui_node title = shell_grow(UI_COLUMN);
    title.align_x = UI_ALIGN_CENTER;
    title.align_y = UI_ALIGN_CENTER;
    /* At least 140 points each side, so the title stays centred until a
     * Reconnect button widens the right one. */
    ui_node right = frame_box();
    right.width.mode = UI_SIZE_FIT;
    right.width.min = 140;
    right.align_x = UI_ALIGN_END;
    right.align_y = UI_ALIGN_CENTER;
    right.gap = 6;
    ui_node dot = frame_box();
    dot.width.mode = UI_SIZE_FIXED;
    dot.width.min = 8;
    dot.height.mode = UI_SIZE_FIXED;
    dot.height.min = 8;
    dot.radius = 4;
    dot.background = theme_color(s->dark, connection_token(s->connection));
    ui_node left = right;
    left.align_x = UI_ALIGN_START;
    return frame_open(b, bar) && frame_open(b, left) &&
           nav_button(b, s, SC_STR_LIT(b->arena, "\xE2\x80\xB9")) && /* ‹ */
           nav_button(b, s, SC_STR_LIT(b->arena, "\xE2\x80\xBA")) && /* › */
           frame_close(b) && frame_open(b, title) &&
           shell_text(b, s, s->object, UI_FONT_SANS_MEDIUM, THEME_TEXT_SIZE, THEME_TEXT) &&
           shell_text(b, s, s->server, UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, THEME_SECONDARY_TEXT) &&
           frame_close(b) && frame_open(b, right) && frame_leaf(b, dot) &&
           shell_text(b, s, s->connection_label, UI_FONT_SANS, THEME_SMALL_TEXT_SIZE,
                THEME_SECONDARY_TEXT) &&
           (s->reconnect == SHELL_RECONNECT_NONE ||
            shell_button(b, s->dark, s->reconnect_label, FOCUS_TAG_RECONNECT_BAR,
                         s->reconnect == SHELL_RECONNECT_ENABLED ? SHELL_BUTTON_PLAIN
                                                                 : SHELL_BUTTON_DISABLED)) &&
           frame_close(b) && frame_close(b) && shell_rule(b, s);
}

/* A boxed sidebar pane: a small caps heading and a grey
 * caption saying what will appear there. */
static bool sidebar_box(frame_builder *b, const shell_state *s, sc_str heading, sc_str caption,
                        bool grows) {
    /* The Queries box is at most 150 points tall and gives way first when the
     * window is short, so the bottom bar always stays on screen. */
    ui_node box = shell_grow(UI_COLUMN);
    if (!grows) {
        box.height.min = 60;
        box.height.max = 150;
    }
    box = pad(box, 8, 6);
    box.gap = 4;
    box.radius = THEME_BOX_RADIUS;
    box.border = 1;
    box.border_color = theme_color(s->dark, THEME_SIDEBAR_BORDER);
    box.background = theme_color(s->dark, THEME_CONTENT);
    ui_node row = fixed_height(THEME_SIDEBAR_ROW_HEIGHT);
    return frame_open(b, box) && frame_open(b, row) &&
           shell_text(b, s, heading, UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE, THEME_SECONDARY_TEXT) &&
           frame_close(b) &&
           shell_text(b, s, caption, UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, THEME_SECONDARY_TEXT) &&
           frame_close(b);
}

static bool sidebar(frame_builder *b, const shell_state *s) {
    ui_node col = frame_box();
    col.width.mode = UI_SIZE_FIXED;
    col.width.min = THEME_SIDEBAR_WIDTH;
    col.height.mode = UI_SIZE_GROW;
    col.direction = UI_COLUMN;
    col.gap = THEME_INSET;
    col.tag = FOCUS_TAG_SIDEBAR;
    return frame_open(b, col) &&
           sidebar_box(b, s, SC_STR_LIT(b->arena, "QUERIES"),
                       SC_STR_LIT(b->arena, "Saved queries will appear here."), false) &&
           sidebar_box(b, s, SC_STR_LIT(b->arena, "TABLES"),
                       SC_STR_LIT(b->arena, "Tables and views will appear here."), true) &&
           frame_close(b);
}

static bool bottom_bar(frame_builder *b, const shell_state *s) {
    ui_node bar = pad(fixed_height(THEME_BOTTOM_BAR_HEIGHT), 10, 0);
    bar.background = theme_color(s->dark, THEME_BOTTOM_BAR);
    bar.gap = 8;
    ui_node third = shell_grow(UI_ROW);
    third.align_y = UI_ALIGN_CENTER;
    third.gap = 4;
    ui_node centre = third;
    centre.align_x = UI_ALIGN_CENTER;
    ui_node right = third;
    right.align_x = UI_ALIGN_END;
    bool status = s->status != SHELL_STATUS_NONE;
    bool ok = s->status == SHELL_STATUS_OK;
    sc_str mark = ok ? SC_STR_LIT(b->arena, "\xE2\x9C\x93") : SC_STR_LIT(b->arena, "\xE2\x9C\x97");
    return shell_rule(b, s) && frame_open(b, bar) && frame_open(b, third) &&
           shell_text(b, s, s->result_label, UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, THEME_SECONDARY_TEXT) &&
           frame_close(b) && frame_open(b, centre) &&
           shell_text(b, s, s->result_count, UI_FONT_SANS, THEME_SMALL_TEXT_SIZE, THEME_TEXT) &&
           frame_close(b) && frame_open(b, right) &&
           (!status || shell_text(b, s, mark, UI_FONT_SANS_MEDIUM, THEME_SMALL_TEXT_SIZE,
                            ok ? THEME_SUCCESS : THEME_ERROR)) &&
           (!status || shell_text(b, s, s->elapsed, UI_FONT_SANS, THEME_SMALL_TEXT_SIZE,
                            THEME_SECONDARY_TEXT)) &&
           frame_close(b) && frame_close(b);
}

bool shell_open(frame_builder *b, const shell_state *s) {
    ui_node window = shell_grow(UI_COLUMN);
    window.background = theme_color(s->dark, THEME_WINDOW);
    ui_node body = pad(shell_grow(UI_ROW), THEME_INSET, THEME_INSET);
    body.gap = THEME_INSET;
    /* The content column: the tab strip over the view, as in Postico (U16),
     * so the sidebar runs from the toolbar to the bottom edge. */
    ui_node column = shell_grow(UI_COLUMN);
    /* Square, unlike the sidebar boxes: the bottom bar runs to its corners. */
    ui_node box = shell_grow(UI_COLUMN);
    box.border = 1;
    box.border_color = theme_color(s->dark, THEME_SIDEBAR_BORDER);
    box.background = theme_color(s->dark, THEME_CONTENT);
    return frame_open(b, window) && toolbar(b, s) && frame_open(b, body) &&
           (!s->show_sidebar || sidebar(b, s)) && frame_open(b, column) &&
           (s->strip == NULL || s->strip(b, s->strip_ctx)) && frame_open(b, box);
}

bool shell_close(frame_builder *b, const shell_state *s) {
    return bottom_bar(b, s) && frame_close(b) && frame_close(b) && frame_close(b) &&
           frame_close(b);
}

bool shell_build(frame_builder *b, const shell_state *s) {
    ui_node view = shell_grow(UI_COLUMN);
    view.align_x = UI_ALIGN_CENTER;
    view.align_y = UI_ALIGN_CENTER;
    return shell_open(b, s) && frame_open(b, view) &&
           shell_text(b, s, s->placeholder, UI_FONT_SANS, THEME_TEXT_SIZE, THEME_SECONDARY_TEXT) &&
           frame_close(b) && shell_close(b, s);
}
