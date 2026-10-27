/* Tests the explorer's focus model (examples/explorer/focus.c, plan U16): a
 * key goes to the global table first, then to the focused area's map, and
 * otherwise nowhere; text goes only to the editor; a left click focuses the
 * area it lands on. Core code, built in with the key maps it routes to. */
#include "../examples/explorer/frame.c"
#include "../examples/explorer/grid.c"
#include "../examples/explorer/input.c"
#include "../examples/explorer/tabs.c"
#include "../examples/explorer/focus.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>

alignas(8) static uint8_t mem[4096];
static sc_arena A;

static const focus_area areas[] = {FOCUS_EDITOR, FOCUS_GRID, FOCUS_DETAIL, FOCUS_SIDEBAR,
                                   FOCUS_PROMPT};
#define AREAS (sizeof areas / sizeof areas[0])

static const uint8_t mac = UI_PLATFORM_MAC, other = UI_PLATFORM_OTHER;

/* With the grid focused, a typed letter reaches neither the SQL box nor the
 * grid: the text event is not taken, and the key itself maps to nothing. */
static void letters_with_the_grid_focused(void) {
    assert(!focus_takes_text(FOCUS_GRID));
    assert(focus_route(FOCUS_GRID, 'a', 0, mac, false).to == FOCUS_TO_NONE);
    assert(focus_route(FOCUS_GRID, 'a', UI_MOD_SHIFT, other, false).to == FOCUS_TO_NONE);
    /* Nor do the editor's own keys. */
    assert(focus_route(FOCUS_GRID, UI_KEY_BACKSPACE, 0, mac, false).to == FOCUS_TO_NONE);
    assert(focus_route(FOCUS_GRID, UI_KEY_TAB, 0, mac, false).to == FOCUS_TO_NONE);
    assert(focus_takes_text(FOCUS_EDITOR));
    for (size_t i = 1; i < AREAS; i++) {
        assert(!focus_takes_text(areas[i]));
    }
}

/* The global table wins whichever area has focus. */
static void global_keys(void) {
    for (size_t i = 0; i < AREAS; i++) {
        focus_key k = focus_route(areas[i], 't', UI_MOD_CMD, mac, false);
        assert(k.to == FOCUS_TO_TAB && k.tab.kind == TAB_KEY_NEW);
        k = focus_route(areas[i], 't', UI_MOD_CMD | UI_MOD_CTRL, other, false);
        assert(k.to == FOCUS_TO_TAB && k.tab.kind == TAB_KEY_NEW);
        k = focus_route(areas[i], '2', UI_MOD_CMD, mac, false);
        assert(k.to == FOCUS_TO_TAB && k.tab.kind == TAB_KEY_SELECT && k.tab.tab == 1);
        assert(focus_route(areas[i], '0', UI_MOD_CMD | UI_MOD_ALT, mac, false).to == FOCUS_TO_DETAIL);
        assert(focus_route(areas[i], UI_KEY_RETURN, UI_MOD_CMD, mac, false).to == FOCUS_TO_RUN);
        assert(focus_route(areas[i], UI_KEY_KP_ENTER, UI_MOD_CMD | UI_MOD_CTRL, other, false).to ==
               FOCUS_TO_RUN);
        k = focus_route(areas[i], UI_KEY_RETURN, UI_MOD_CMD | UI_MOD_SHIFT, mac, false);
        assert(k.to == FOCUS_TO_TAB && k.tab.kind == TAB_KEY_LOAD_ALL);
        /* Esc cancels the tab's active query first, wherever the focus is. */
        assert(focus_route(areas[i], UI_KEY_ESCAPE, 0, mac, true).to == FOCUS_TO_CANCEL);
        /* Only ⌘ makes these keys: ^0 with ⌥ and ^↩ on the Mac do nothing. */
        assert(focus_route(areas[i], '0', UI_MOD_CTRL | UI_MOD_ALT, mac, false).to == FOCUS_TO_NONE);
        assert(focus_route(areas[i], UI_KEY_RETURN, UI_MOD_CTRL, mac, false).to != FOCUS_TO_RUN);
    }
}

/* Then the focused area's own map, else nowhere. */
static void area_maps(void) {
    focus_key k = focus_route(FOCUS_EDITOR, UI_KEY_LEFT, 0, mac, false);
    assert(k.to == FOCUS_TO_EDITOR && k.input == INPUT_KEY_LEFT);
    k = focus_route(FOCUS_EDITOR, UI_KEY_RETURN, 0, mac, false);
    assert(k.to == FOCUS_TO_EDITOR && k.input == INPUT_KEY_NEWLINE);
    k = focus_route(FOCUS_GRID, UI_KEY_DOWN, 0, mac, false);
    assert(k.to == FOCUS_TO_GRID && k.grid == GRID_KEY_DOWN);
    k = focus_route(FOCUS_GRID, UI_KEY_ESCAPE, 0, mac, false); /* no query: clears the selection */
    assert(k.to == FOCUS_TO_GRID && k.grid == GRID_KEY_CLEAR);
    assert(focus_route(FOCUS_EDITOR, UI_KEY_ESCAPE, 0, mac, false).to == FOCUS_TO_NONE);
    assert(focus_route(FOCUS_EDITOR, UI_KEY_PAGE_DOWN, 0, mac, false).to == FOCUS_TO_NONE);
    assert(focus_route(FOCUS_DETAIL, UI_KEY_DOWN, 0, mac, false).to == FOCUS_TO_NONE);
    assert(focus_route(FOCUS_SIDEBAR, UI_KEY_DOWN, 0, mac, false).to == FOCUS_TO_NONE);
    assert(focus_route(FOCUS_PROMPT, UI_KEY_LEFT, 0, mac, false).to == FOCUS_TO_NONE);
}

/* KTD26 through the router: the platform is a parameter, so both run here. */
static void platforms(void) {
    focus_key k = focus_route(FOCUS_EDITOR, 'a', UI_MOD_CTRL, mac, false);
    assert(k.to == FOCUS_TO_EDITOR && k.input == INPUT_KEY_LINE_START);
    k = focus_route(FOCUS_EDITOR, 'a', UI_MOD_CMD, mac, false);
    assert(k.to == FOCUS_TO_EDITOR && k.input == INPUT_KEY_SELECT_ALL);
    k = focus_route(FOCUS_EDITOR, 'a', UI_MOD_CMD | UI_MOD_CTRL, other, false);
    assert(k.to == FOCUS_TO_EDITOR && k.input == INPUT_KEY_SELECT_ALL);
}

/* Only a left click moves the focus, to the area it lands on. */
static void clicks(void) {
    assert(focus_click(FOCUS_EDITOR, FOCUS_TAG_GRID, UI_BUTTON_LEFT) == FOCUS_GRID);
    assert(focus_click(FOCUS_EDITOR, FOCUS_TAG_GRID, UI_BUTTON_RIGHT) == FOCUS_EDITOR);
    assert(focus_click(FOCUS_EDITOR, FOCUS_TAG_GRID, UI_BUTTON_MIDDLE) == FOCUS_EDITOR);
    assert(focus_click(FOCUS_GRID, FOCUS_TAG_EDITOR, UI_BUTTON_LEFT) == FOCUS_EDITOR);
    /* The sidebar, detail pane and prompt have no key map yet: a click there
     * leaves the keys where they were, so typing still reaches the SQL box. */
    for (size_t i = 0; i < 2; i++) {
        assert(focus_click(areas[i], FOCUS_TAG_DETAIL, UI_BUTTON_LEFT) == areas[i]);
        assert(focus_click(areas[i], FOCUS_TAG_SIDEBAR, UI_BUTTON_LEFT) == areas[i]);
        assert(focus_click(areas[i], FOCUS_TAG_PROMPT, UI_BUTTON_LEFT) == areas[i]);
    }
    /* Off every area (the bottom bar, a tab): the focus stays. */
    assert(focus_click(FOCUS_GRID, 0, UI_BUTTON_LEFT) == FOCUS_GRID);
    assert(focus_click(FOCUS_GRID, TAB_TAG_SELECT, UI_BUTTON_LEFT) == FOCUS_GRID);
}

/* An area the last frame did not draw gives the keyboard to the editor. */
static void areas_must_be_shown(void) {
    ui_layout l = {&A, SC_TSLICE_ALLOC(ui_rect, &A, 4), 0};
    ui_rect editor = {FOCUS_TAG_EDITOR, 0, 0, 100, 100};
    assert(SC_TSLICE_SET(ui_rect, &A, l.rects, 0, &editor));
    l.count = 1;
    assert(focus_shown(FOCUS_GRID, &l) == FOCUS_EDITOR);
    assert(focus_shown(FOCUS_DETAIL, &l) == FOCUS_EDITOR);
    assert(focus_shown(FOCUS_EDITOR, &l) == FOCUS_EDITOR);
    ui_rect grid = {FOCUS_TAG_GRID, 0, 100, 100, 100};
    assert(SC_TSLICE_SET(ui_rect, &A, l.rects, 1, &grid));
    l.count = 2;
    assert(focus_shown(FOCUS_GRID, &l) == FOCUS_GRID);
}

int main(void) {
    sc_arena_init(&A, mem, sizeof mem);
    letters_with_the_grid_focused();
    global_keys();
    area_maps();
    platforms();
    clicks();
    areas_must_be_shown();
    puts("explorer_focus_test: ok");
    return 0;
}
