/* Tests the explorer's focus model (examples/explorer/focus.c, plan U16): a
 * key goes to the global table first, then to the focused area's map, and
 * otherwise nowhere; text goes only to the editor; a left click focuses the
 * area it lands on. ⌘0 toggles the sidebar from anywhere, and the focused
 * sidebar takes the arrows, ↩ and ⌘↩, ahead of ⌘↩'s run (U19). Core code,
 * built in with the key maps it routes to. */
#include "../examples/explorer/frame.c"
#include "../examples/explorer/grid.c"
#include "../examples/explorer/sqltok.c"
#include "../examples/explorer/sqlkw.c"
#include "../examples/explorer/editor.c"
#include "../examples/explorer/editor_syntax.c"
#include "../examples/explorer/tabs.c"
#include "../examples/explorer/proto.c"
#include "../examples/explorer/result.c"
#include "../examples/explorer/catalog.c"
#include "../examples/explorer/catalog_sql.c"
#include "../examples/explorer/theme.c"
#include "../examples/explorer/tree.c"
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
    assert(focus_route(FOCUS_GRID, 'a', 0, mac).to == FOCUS_TO_NONE);
    assert(focus_route(FOCUS_GRID, 'a', UI_MOD_SHIFT, other).to == FOCUS_TO_NONE);
    /* Nor do the editor's own keys. */
    assert(focus_route(FOCUS_GRID, UI_KEY_BACKSPACE, 0, mac).to == FOCUS_TO_NONE);
    assert(focus_route(FOCUS_GRID, UI_KEY_TAB, 0, mac).to == FOCUS_TO_NONE);
    assert(focus_takes_text(FOCUS_EDITOR));
    for (size_t i = 1; i < AREAS; i++) {
        assert(!focus_takes_text(areas[i]));
    }
}

/* The global table wins whichever area has focus. */
static void global_keys(void) {
    for (size_t i = 0; i < AREAS; i++) {
        focus_key k = focus_route(areas[i], 't', UI_MOD_CMD, mac);
        assert(k.to == FOCUS_TO_TAB && k.tab.kind == TAB_KEY_NEW);
        k = focus_route(areas[i], 't', UI_MOD_CMD | UI_MOD_CTRL, other);
        assert(k.to == FOCUS_TO_TAB && k.tab.kind == TAB_KEY_NEW);
        k = focus_route(areas[i], '2', UI_MOD_CMD, mac);
        assert(k.to == FOCUS_TO_TAB && k.tab.kind == TAB_KEY_SELECT && k.tab.tab == 1);
        assert(focus_route(areas[i], '0', UI_MOD_CMD | UI_MOD_ALT, mac).to == FOCUS_TO_DETAIL);
        assert(focus_route(areas[i], '0', UI_MOD_CMD, mac).to == FOCUS_TO_SIDEBAR);
        assert(focus_route(areas[i], '0', UI_MOD_CMD | UI_MOD_CTRL, other).to == FOCUS_TO_SIDEBAR);
        assert(focus_route(areas[i], '0', UI_MOD_CTRL, mac).to == FOCUS_TO_NONE);
        assert(focus_route(areas[i], '0', UI_MOD_CMD | UI_MOD_SHIFT, mac).to == FOCUS_TO_NONE);
        if (areas[i] != FOCUS_SIDEBAR) {
            assert(focus_route(areas[i], UI_KEY_RETURN, UI_MOD_CMD, mac).to == FOCUS_TO_RUN);
            assert(focus_route(areas[i], UI_KEY_KP_ENTER, UI_MOD_CMD | UI_MOD_CTRL, other).to ==
                   FOCUS_TO_RUN);
        }
        k = focus_route(areas[i], UI_KEY_RETURN, UI_MOD_CMD | UI_MOD_SHIFT, mac);
        assert(k.to == FOCUS_TO_TAB && k.tab.kind == TAB_KEY_LOAD_ALL);
        /* ⌘. cancels and ⌥⌘. disconnects, wherever the focus is (U13); on
         * Linux Ctrl is the command key, and sets both bits. */
        assert(focus_route(areas[i], '.', UI_MOD_CMD, mac).to == FOCUS_TO_CANCEL);
        assert(focus_route(areas[i], '.', UI_MOD_CMD | UI_MOD_CTRL, other).to == FOCUS_TO_CANCEL);
        assert(focus_route(areas[i], '.', UI_MOD_CMD | UI_MOD_ALT, mac).to == FOCUS_TO_DISCONNECT);
        assert(focus_route(areas[i], '.', UI_MOD_CMD | UI_MOD_CTRL | UI_MOD_ALT, other).to ==
               FOCUS_TO_DISCONNECT);
        /* The Mac's own Control is not ⌘: ^. and ^⌥. do nothing there. */
        assert(focus_route(areas[i], '.', UI_MOD_CTRL, mac).to != FOCUS_TO_CANCEL);
        assert(focus_route(areas[i], '.', UI_MOD_CTRL | UI_MOD_ALT, mac).to != FOCUS_TO_DISCONNECT);
        /* Esc no longer cancels: it is the focused area's key. */
        assert(focus_route(areas[i], UI_KEY_ESCAPE, 0, mac).to != FOCUS_TO_CANCEL);
        /* Only ⌘ makes these keys: ^0 with ⌥ and ^↩ on the Mac do nothing. */
        assert(focus_route(areas[i], '0', UI_MOD_CTRL | UI_MOD_ALT, mac).to == FOCUS_TO_NONE);
        assert(focus_route(areas[i], UI_KEY_RETURN, UI_MOD_CTRL, mac).to != FOCUS_TO_RUN);
    }
}

/* Then the focused area's own map, else nowhere. */
static void area_maps(void) {
    focus_key k = focus_route(FOCUS_EDITOR, UI_KEY_LEFT, 0, mac);
    assert(k.to == FOCUS_TO_EDITOR && k.edit.cmd == EDITOR_CMD_LEFT);
    k = focus_route(FOCUS_EDITOR, UI_KEY_RETURN, 0, mac);
    assert(k.to == FOCUS_TO_EDITOR && k.edit.cmd == EDITOR_CMD_NEWLINE);
    k = focus_route(FOCUS_GRID, UI_KEY_DOWN, 0, mac);
    assert(k.to == FOCUS_TO_GRID && k.grid == GRID_KEY_DOWN);
    k = focus_route(FOCUS_GRID, UI_KEY_ESCAPE, 0, mac); /* clears the selection */
    assert(k.to == FOCUS_TO_GRID && k.grid == GRID_KEY_CLEAR);
    k = focus_route(FOCUS_EDITOR, UI_KEY_ESCAPE, 0, mac);
    assert(k.to == FOCUS_TO_EDITOR && k.edit.cmd == EDITOR_CMD_ESCAPE);
    assert(focus_route(FOCUS_EDITOR, UI_KEY_RETURN, UI_MOD_CMD | UI_MOD_ALT, mac).to == FOCUS_TO_RUN_ALL);
    assert(focus_route(FOCUS_GRID, UI_KEY_RETURN, UI_MOD_CMD, mac).to == FOCUS_TO_RUN);
    assert(focus_route(FOCUS_EDITOR, UI_KEY_PAGE_DOWN, 0, mac).to == FOCUS_TO_NONE);
    assert(focus_route(FOCUS_DETAIL, UI_KEY_DOWN, 0, mac).to == FOCUS_TO_NONE);
    assert(focus_route(FOCUS_PROMPT, UI_KEY_LEFT, 0, mac).to == FOCUS_TO_NONE);
    k = focus_route(FOCUS_SIDEBAR, UI_KEY_DOWN, 0, mac);
    assert(k.to == FOCUS_TO_TREE && k.tree == TREE_KEY_DOWN);
    k = focus_route(FOCUS_SIDEBAR, UI_KEY_LEFT, 0, other);
    assert(k.to == FOCUS_TO_TREE && k.tree == TREE_KEY_LEFT);
    k = focus_route(FOCUS_SIDEBAR, UI_KEY_RETURN, 0, mac);
    assert(k.to == FOCUS_TO_TREE && k.tree == TREE_KEY_OPEN);
    k = focus_route(FOCUS_SIDEBAR, UI_KEY_RETURN, UI_MOD_CMD, mac);
    assert(k.to == FOCUS_TO_TREE && k.tree == TREE_KEY_OPEN_NEW);
    k = focus_route(FOCUS_SIDEBAR, UI_KEY_KP_ENTER, UI_MOD_CMD | UI_MOD_CTRL, other);
    assert(k.to == FOCUS_TO_TREE && k.tree == TREE_KEY_OPEN_NEW);
    k = focus_route(FOCUS_SIDEBAR, UI_KEY_RETURN, UI_MOD_CMD | UI_MOD_SHIFT, mac);
    assert(k.to == FOCUS_TO_TAB && k.tab.kind == TAB_KEY_LOAD_ALL);
    assert(focus_route(FOCUS_SIDEBAR, '.', UI_MOD_CMD, mac).to == FOCUS_TO_CANCEL);
    assert(focus_route(FOCUS_SIDEBAR, 'a', 0, mac).to == FOCUS_TO_NONE);
    assert(focus_route(FOCUS_GRID, UI_KEY_LEFT, 0, mac).to == FOCUS_TO_NONE);
}

/* KTD26 through the router: the platform is a parameter, so both run here. */
static void platforms(void) {
    focus_key k = focus_route(FOCUS_EDITOR, 'a', UI_MOD_CTRL, mac);
    assert(k.to == FOCUS_TO_EDITOR && k.edit.cmd == EDITOR_CMD_LINE_START);
    k = focus_route(FOCUS_EDITOR, 'a', UI_MOD_CMD, mac);
    assert(k.to == FOCUS_TO_EDITOR && k.edit.cmd == EDITOR_CMD_SELECT_ALL);
    k = focus_route(FOCUS_EDITOR, 'a', UI_MOD_CMD | UI_MOD_CTRL, other);
    assert(k.to == FOCUS_TO_EDITOR && k.edit.cmd == EDITOR_CMD_SELECT_ALL);
}

/* Only a left click moves the focus, to the area it lands on. */
static void clicks(void) {
    assert(focus_click(FOCUS_EDITOR, FOCUS_TAG_GRID, UI_BUTTON_LEFT) == FOCUS_GRID);
    assert(focus_click(FOCUS_EDITOR, FOCUS_TAG_GRID, UI_BUTTON_RIGHT) == FOCUS_EDITOR);
    assert(focus_click(FOCUS_EDITOR, FOCUS_TAG_GRID, UI_BUTTON_MIDDLE) == FOCUS_EDITOR);
    assert(focus_click(FOCUS_GRID, FOCUS_TAG_EDITOR, UI_BUTTON_LEFT) == FOCUS_EDITOR);
    for (size_t i = 0; i < 2; i++) {
        assert(focus_click(areas[i], FOCUS_TAG_DETAIL, UI_BUTTON_LEFT) == areas[i]);
        assert(focus_click(areas[i], FOCUS_TAG_PROMPT, UI_BUTTON_LEFT) == areas[i]);
        assert(focus_click(areas[i], FOCUS_TAG_SIDEBAR, UI_BUTTON_LEFT) == FOCUS_SIDEBAR);
        assert(focus_click(areas[i], FOCUS_TAG_SIDEBAR_ROWS, UI_BUTTON_LEFT) == FOCUS_SIDEBAR);
        assert(focus_click(areas[i], FOCUS_TAG_SIDEBAR_ROWS, UI_BUTTON_RIGHT) == areas[i]);
        assert(focus_click(areas[i], FOCUS_TAG_SIDEBAR_TABLES, UI_BUTTON_LEFT) == FOCUS_SIDEBAR);
        assert(focus_click(areas[i], FOCUS_TAG_SIDEBAR_FUNCTIONS, UI_BUTTON_LEFT) == FOCUS_SIDEBAR);
        assert(focus_click(areas[i], FOCUS_TAG_SIDEBAR_RELOAD, UI_BUTTON_LEFT) == FOCUS_SIDEBAR);
    }
    assert(focus_click(FOCUS_SIDEBAR, FOCUS_TAG_EDITOR, UI_BUTTON_LEFT) == FOCUS_EDITOR);
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
    assert(focus_shown(FOCUS_SIDEBAR, &l) == FOCUS_EDITOR);
    ui_rect side = {FOCUS_TAG_SIDEBAR, 0, 0, 50, 100};
    assert(SC_TSLICE_SET(ui_rect, &A, l.rects, 2, &side));
    l.count = 3;
    assert(focus_shown(FOCUS_SIDEBAR, &l) == FOCUS_SIDEBAR);
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
