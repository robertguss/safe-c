#include "focus.h"

focus_key focus_route(focus_area area, uint32_t key, uint16_t mods, uint8_t platform,
                      bool query_active) {
    focus_key k = {FOCUS_TO_NONE, {TAB_KEY_NONE, 0}, INPUT_KEY_NONE, GRID_KEY_NONE};
    bool cmd = (mods & UI_MOD_CMD) != 0 &&
               !(platform == UI_PLATFORM_MAC && (mods & UI_MOD_CTRL) != 0);
    bool alt = (mods & UI_MOD_ALT) != 0;
    k.tab = tab_key_for(key, mods, platform);
    if (k.tab.kind != TAB_KEY_NONE) {
        k.to = FOCUS_TO_TAB;
    } else if (cmd && alt && key == '0') {
        k.to = FOCUS_TO_DETAIL;
    } else if (cmd && (key == UI_KEY_RETURN || key == UI_KEY_KP_ENTER)) {
        k.to = FOCUS_TO_RUN;
    } else if (key == UI_KEY_ESCAPE && query_active) {
        k.to = FOCUS_TO_CANCEL;
    } else if (area == FOCUS_EDITOR) {
        k.input = input_key_for(key, mods, platform);
        k.to = k.input != INPUT_KEY_NONE ? FOCUS_TO_EDITOR : FOCUS_TO_NONE;
    } else if (area == FOCUS_GRID) {
        k.grid = grid_key_for(key, mods, platform);
        k.to = k.grid != GRID_KEY_NONE ? FOCUS_TO_GRID : FOCUS_TO_NONE;
    }
    return k;
}

bool focus_takes_text(focus_area area) {
    return area == FOCUS_EDITOR;
}

static uint32_t tag_of(focus_area area) {
    switch (area) {
    case FOCUS_GRID:
        return FOCUS_TAG_GRID;
    case FOCUS_DETAIL:
        return FOCUS_TAG_DETAIL;
    case FOCUS_SIDEBAR:
        return FOCUS_TAG_SIDEBAR;
    case FOCUS_PROMPT:
        return FOCUS_TAG_PROMPT;
    default:
        return FOCUS_TAG_EDITOR;
    }
}

focus_area focus_click(focus_area area, uint32_t tag, uint8_t button) {
    if (button != UI_BUTTON_LEFT) {
        return area;
    }
    switch (tag) {
    case FOCUS_TAG_EDITOR:
        return FOCUS_EDITOR;
    case FOCUS_TAG_GRID:
        return FOCUS_GRID;
    case FOCUS_TAG_DETAIL:
        return FOCUS_DETAIL;
    case FOCUS_TAG_SIDEBAR:
        return FOCUS_SIDEBAR;
    case FOCUS_TAG_PROMPT:
        return FOCUS_PROMPT;
    default:
        return area;
    }
}

focus_area focus_shown(focus_area area, const ui_layout *l) {
    ui_rect r;
    return area == FOCUS_EDITOR || frame_rect(l, tag_of(area), &r) ? area : FOCUS_EDITOR;
}
