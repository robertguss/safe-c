#include "panes.h"
#include "focus.h"
#include "theme.h"
#include "../../adapters/sc_ui.h"

static uint32_t pane_clamp(uint32_t n, uint32_t least, uint32_t most) {
    most = most < least ? least : most;
    most = most > UINT16_MAX ? UINT16_MAX : most;
    return n < least ? least : n > most ? most : n;
}

uint32_t panes_sidebar_width(uint32_t requested, uint32_t window) {
    uint32_t chrome = 3u * THEME_INSET + 2u + 320u;
    return pane_clamp(requested == 0 ? THEME_SIDEBAR_WIDTH : requested, 180u,
                      window > chrome ? window - chrome : 0u);
}

uint32_t panes_editor_height(uint32_t requested, uint32_t preferred, uint32_t room) {
    return pane_clamp(requested == 0 ? preferred : requested, 80u, room > 96u ? room - 96u : 0u);
}

uint32_t panes_hit(const ui_layout *l, int32_t x, int32_t y) {
    ui_rect r;
    if (frame_rect(l, PANES_TAG_SIDEBAR, &r) && (int64_t)x >= (int64_t)r.x - 1 &&
        (int64_t)x < (int64_t)r.x + r.w + 1 && y >= r.y && (int64_t)y < (int64_t)r.y + r.h) {
        return PANES_TAG_SIDEBAR;
    }
    if (frame_rect(l, PANES_TAG_EDITOR, &r) && x >= r.x && (int64_t)x < (int64_t)r.x + r.w &&
        (int64_t)y >= (int64_t)r.y - 3 && (int64_t)y < (int64_t)r.y + r.h + 3) {
        return PANES_TAG_EDITOR;
    }
    return 0;
}

uint8_t panes_cursor(const pane_state *p, const ui_layout *l, int32_t x, int32_t y) {
    uint32_t tag = p->drag != 0 ? p->drag : panes_hit(l, x, y);
    return tag == PANES_TAG_SIDEBAR ? UI_CURSOR_RESIZE_EW
           : tag == PANES_TAG_EDITOR ? UI_CURSOR_RESIZE_NS
           : frame_hit(l, x, y) == FOCUS_TAG_EDITOR ? UI_CURSOR_TEXT : UI_CURSOR_DEFAULT;
}

bool panes_event(pane_state *p, const ui_layout *l, ui_event ev) {
    ui_rect r;
    if (ev.kind == UI_EVENT_FOCUS_LOST || ev.kind == UI_EVENT_RESIZE ||
        (p->drag != 0 && !frame_rect(l, p->drag, &r))) {
        bool active = p->drag != 0;
        p->drag = 0;
        return active;
    }
    if (ev.kind == UI_EVENT_POINTER_DOWN && ev.button == UI_BUTTON_LEFT) {
        uint32_t tag = panes_hit(l, ev.x, ev.y);
        bool side = tag == PANES_TAG_SIDEBAR;
        if (tag == 0 || !frame_rect(l, side ? FOCUS_TAG_SIDEBAR : FOCUS_TAG_EDITOR, &r)) return false;
        p->drag = tag;
        p->offset = side ? ev.x - r.w : ev.y - r.h;
        return true;
    }
    if (p->drag == 0) return false;
    if (ev.kind == UI_EVENT_POINTER_UP && ev.button != UI_BUTTON_LEFT) return false;
    if (ev.kind != UI_EVENT_POINTER_MOVE && ev.kind != UI_EVENT_POINTER_UP) return false;
    bool side = p->drag == PANES_TAG_SIDEBAR;
    int64_t n = (int64_t)(side ? ev.x : ev.y) - p->offset;
    uint32_t size = n < 1 ? 1u : n > UINT16_MAX ? UINT16_MAX : (uint32_t)n;
    if (side) p->sidebar = size;
    else p->editor = size;
    if (ev.kind == UI_EVENT_POINTER_UP) p->drag = 0;
    return true;
}
