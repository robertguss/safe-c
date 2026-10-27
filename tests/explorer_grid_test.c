/* Tests the explorer's grid geometry and selection (examples/explorer/grid.c):
 * the plan's U11 boundaries. Core code, built in. */
#include "../examples/explorer/frame.c"
#include "../examples/explorer/grid.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>

static grid_geom geom(uint64_t rows, uint64_t scroll) {
    /* A 460-point body under a 19-point header. */
    grid_geom g = {rows, 23, 19, 800, 19 + 460, scroll, 0, 800};
    return g;
}

static void visible_rows(void) {
    grid_rows r = grid_visible(geom(1000, 0));
    assert(r.first == 0 && r.count == 20 && r.top == 0); /* rows 0..19 */
    r = grid_visible(geom(1000, 22));
    assert(r.first == 0 && r.count == 21 && r.top == -22); /* rows 0..20 */
    r = grid_visible(geom(1000, 23));
    assert(r.first == 1 && r.count == 20 && r.top == 0); /* rows 1..20 */
    /* Past the end clamps to the last full page. */
    grid_geom g = geom(1000, 999999);
    assert(grid_max_scroll_y(g) == 1000u * 23u - 460u);
    r = grid_visible(g);
    assert(r.first + r.count - 1u == 999 && r.count == 20);
    /* Fewer rows than fit: all of them, no scroll. */
    g = geom(5, 40);
    assert(grid_max_scroll_y(g) == 0 && grid_clamp(g).scroll_y == 0);
    r = grid_visible(g);
    assert(r.first == 0 && r.count == 5 && r.top == 0);
    r = grid_visible(geom(0, 0));
    assert(r.count == 0);
    /* A million rows near the end: no overflow, ends at 999,999. */
    g = geom(1000000, 22999977);
    r = grid_visible(g);
    assert(grid_clamp(g).scroll_y == 23000000u - 460u);
    assert(r.first + r.count - 1u == 999999 && r.first == 999980);
    /* A body shorter than one row still shows the row it holds. */
    g = geom(10, 0);
    g.view_h = 19 + 5;
    assert(grid_visible(g).count == 1 && grid_page(g) == 1);
    g.scroll_y = 0; /* a body shorter than a row reveals the row's top */
    assert(grid_reveal(g, 7) == 7u * 23u);
    g.view_h = 10; /* the header fills the region */
    assert(grid_body_h(g) == 0 && grid_visible(g).count == 0);
}

static void columns(void) {
    alignas(8) static uint8_t mem[256];
    sc_arena a;
    sc_arena_init(&a, mem, sizeof mem);
    sc_tslice w = SC_TSLICE_ALLOC(uint32_t, &a, 3);
    const uint32_t ws[3] = {120, 80, 300};
    for (size_t i = 0; i < 3; i++) {
        assert(SC_TSLICE_SET(uint32_t, &a, w, i, &ws[i]));
    }
    grid_geom g = geom(10, 0);
    g.view_w = 250;
    g.total_w = 500;
    g.scroll_x = 100;
    grid_cols c;
    assert(grid_columns(g, &a, w, 3, &c));
    /* x offsets -100, 20, 100: the first column is clipped, not dropped. */
    assert(c.first == 0 && c.left == -100 && c.end == 3);
    g.scroll_x = 120; /* column 0 ends exactly at the left edge */
    assert(grid_columns(g, &a, w, 3, &c) && c.first == 1 && c.left == 0 && c.end == 3);
    g.scroll_x = 0;
    g.view_w = 200; /* columns 0 and 1 fill it exactly */
    assert(grid_columns(g, &a, w, 3, &c) && c.first == 0 && c.end == 2);
    g.view_w = 201;
    assert(grid_columns(g, &a, w, 3, &c) && c.end == 3);
    g.scroll_x = 9999; /* clamped to 500 - 201 */
    assert(grid_columns(g, &a, w, 3, &c) && c.first == 2 && c.left == 200 - 299);
    sc_tslice none = {{NULL, 0, a.epoch}, 0};
    assert(!grid_columns(g, &a, none, 3, &c));
}

static void hits(void) {
    grid_geom g = geom(100, 0);
    uint64_t row = 99;
    assert(grid_row_at(g, 19 + 23 * 5 + 1, &row) && row == 5);
    assert(grid_row_at(g, 19, &row) && row == 0);
    assert(!grid_row_at(g, 18, &row)); /* the header */
    assert(!grid_row_at(g, -1, &row) && !grid_row_at(g, 19 + 460, &row));
    g.scroll_y = 10;
    assert(grid_row_at(g, 19 + 12, &row) && row == 0 && grid_row_at(g, 19 + 13, &row) && row == 1);
    g = geom(3, 0);
    assert(!grid_row_at(g, 19 + 23 * 3, &row)); /* below the last row */
}

static void selection(void) {
    grid_sel s = {geom(100, 0), true, 19};
    assert(grid_move(&s, GRID_KEY_DOWN) && s.row == 20 && s.g.scroll_y == 23);
    assert(grid_move(&s, GRID_KEY_UP) && s.row == 19 && s.g.scroll_y == 23); /* still whole */
    s.row = 0;
    s.g.scroll_y = 0;
    assert(!grid_move(&s, GRID_KEY_UP) && s.row == 0);
    assert(grid_move(&s, GRID_KEY_PAGE_DOWN) && s.row == 20 && s.g.scroll_y == 23);
    assert(grid_move(&s, GRID_KEY_PAGE_UP) && s.row == 0 && s.g.scroll_y == 0);
    assert(grid_move(&s, GRID_KEY_LAST) && s.row == 99 && s.g.scroll_y == grid_max_scroll_y(s.g));
    assert(!grid_move(&s, GRID_KEY_DOWN) && s.row == 99);
    s.g.scroll_y = 0; /* the selection scrolled away: ↓ brings it back */
    assert(grid_move(&s, GRID_KEY_DOWN) && s.row == 99 &&
           s.g.scroll_y == grid_max_scroll_y(s.g));
    assert(grid_move(&s, GRID_KEY_FIRST) && s.row == 0 && s.g.scroll_y == 0);
    assert(grid_move(&s, GRID_KEY_CLEAR) && !s.selected && !grid_move(&s, GRID_KEY_CLEAR));
    /* With no selection, the first ↓ selects the first visible row. */
    s.g.scroll_y = 23 * 7 + 5;
    assert(grid_move(&s, GRID_KEY_DOWN) && s.selected && s.row == 7);
    assert(s.g.scroll_y == 23 * 7); /* scrolled the least to show it whole */
    /* A page near the end stops at the last row. */
    s.row = 95;
    assert(grid_move(&s, GRID_KEY_PAGE_DOWN) && s.row == 99);
    /* Click below a 19-point header. */
    grid_sel c = {geom(100, 0), false, 0};
    assert(grid_click(&c, 19 + 23 * 5 + 1) && c.selected && c.row == 5);
    assert(!grid_click(&c, 4) && c.row == 5);
    /* Page Up from the middle, and ⌘↓ / ⌘↑ with nothing selected. */
    grid_sel p = {geom(100, 0), true, 50};
    assert(grid_move(&p, GRID_KEY_PAGE_UP) && p.row == 30);
    grid_sel q = {geom(100, 0), false, 0};
    assert(grid_move(&q, GRID_KEY_LAST) && q.selected && q.row == 99);
    q.selected = false;
    assert(grid_move(&q, GRID_KEY_FIRST) && q.selected && q.row == 0);
    /* A click is a change when only the selection flag or the scroll moves. */
    grid_sel k = {geom(100, 0), true, 5};
    assert(grid_move(&k, GRID_KEY_CLEAR) && grid_click(&k, 19 + 23 * 5 + 1) && k.row == 5);
    k.g.scroll_y = 10; /* row 0 is half under the header */
    k.row = 0;
    assert(grid_click(&k, 19 + 1) && k.row == 0 && k.g.scroll_y == 0);
    assert(!grid_click(&k, 19 + 1)); /* nothing changed */
    grid_sel e = {geom(0, 0), false, 0};
    assert(!grid_move(&e, GRID_KEY_DOWN) && !e.selected);
}

static void wheel(void) {
    grid_sel s = {geom(100, 0), false, 0};
    s.g.total_w = 1200;
    assert(grid_wheel(&s, 0, -50) && s.g.scroll_y == 50); /* down */
    assert(grid_wheel(&s, 0, 80) && s.g.scroll_y == 0);   /* up, stops at 0 */
    assert(!grid_wheel(&s, 0, 10));
    assert(grid_wheel(&s, 0, -100000) && s.g.scroll_y == grid_max_scroll_y(s.g));
    /* SDL's x is positive to the right: the view moves right. */
    assert(grid_wheel(&s, 300, 0) && s.g.scroll_x == 300);
    assert(grid_wheel(&s, 10000, 0) && s.g.scroll_x == 400);
    assert(grid_wheel(&s, -100, 0) && s.g.scroll_x == 300);
    assert(grid_wheel(&s, INT32_MIN, INT32_MAX) && s.g.scroll_x == 0 && s.g.scroll_y == 0);
    assert(grid_wheel(&s, INT32_MAX, INT32_MIN) && s.g.scroll_x == 400);
}

/* ⌘↑ and ⌘↓ go to the ends. On the Mac, Ctrl is its own modifier and makes
 * no grid key (KTD26); on Linux, Ctrl is the command key. */
static void keys(void) {
    uint8_t mac = UI_PLATFORM_MAC, other = UI_PLATFORM_OTHER;
    uint16_t ctrl_cmd = UI_MOD_CMD | UI_MOD_CTRL; /* Ctrl on Linux sets both */
    assert(grid_key_for(UI_KEY_DOWN, 0, mac) == GRID_KEY_DOWN);
    assert(grid_key_for(UI_KEY_DOWN, UI_MOD_CMD, mac) == GRID_KEY_LAST);
    assert(grid_key_for(UI_KEY_UP, ctrl_cmd, other) == GRID_KEY_FIRST);
    assert(grid_key_for(UI_KEY_UP, UI_MOD_CTRL, mac) == GRID_KEY_NONE);
    assert(grid_key_for(UI_KEY_UP, ctrl_cmd, mac) == GRID_KEY_NONE);
    assert(grid_key_for(UI_KEY_PAGE_DOWN, 0, other) == GRID_KEY_PAGE_DOWN);
    assert(grid_key_for(UI_KEY_PAGE_UP, UI_MOD_SHIFT, mac) == GRID_KEY_PAGE_UP);
    assert(grid_key_for(UI_KEY_ESCAPE, 0, mac) == GRID_KEY_CLEAR);
    assert(grid_key_for(UI_KEY_DOWN, UI_MOD_ALT, mac) == GRID_KEY_NONE);
    assert(grid_key_for('a', 0, mac) == GRID_KEY_NONE);
    assert(grid_key_for('0', UI_MOD_CMD | UI_MOD_ALT, mac) == GRID_KEY_NONE); /* a global key */
    assert(grid_key_for(UI_KEY_PAGE_UP, UI_MOD_CMD, mac) == GRID_KEY_NONE);
    assert(grid_key_for(UI_KEY_ESCAPE, UI_MOD_CMD, mac) == GRID_KEY_NONE);
}

static void widen(void) {
    alignas(8) static uint8_t mem[64];
    sc_arena a;
    sc_arena_init(&a, mem, sizeof mem);
    sc_tslice w = SC_TSLICE_ALLOC(uint32_t, &a, 2);
    const uint32_t start[2] = {50, 80};
    for (size_t i = 0; i < 2; i++) {
        assert(SC_TSLICE_SET(uint32_t, &a, w, i, &start[i]));
    }
    uint64_t total = 130;
    uint32_t got = 0;
    assert(grid_widen(&a, w, 0, 70, 300, &total) && total == 150);
    assert(SC_TSLICE_GET(uint32_t, &a, w, 0, &got) && got == 70);
    assert(!grid_widen(&a, w, 0, 60, 300, &total) && total == 150); /* never narrows */
    assert(!grid_widen(&a, w, 0, 70, 300, &total));
    assert(grid_widen(&a, w, 1, 900, 300, &total) && total == 370); /* at most the maximum */
    assert(SC_TSLICE_GET(uint32_t, &a, w, 1, &got) && got == 300);
    assert(!grid_widen(&a, w, 2, 90, 300, &total) && total == 370);
}

int main(void) {
    visible_rows();
    columns();
    hits();
    selection();
    wheel();
    keys();
    widen();
    puts("explorer_grid_test: ok");
    return 0;
}
