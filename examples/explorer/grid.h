#ifndef EXPLORER_GRID_H
#define EXPLORER_GRID_H

/* The result grid's geometry and selection (plan U11): which rows and columns
 * a viewport shows, where a click lands, and how the keys move the selected
 * row. The app owns the scroll state; Clay never scrolls the grid body
 * (KTD3). Core code: integer points, no SDL.
 *
 * The region is `view_w` by `view_h` points: a header band `header_h` high,
 * then the body, scrolled down by `scroll_y` and right by `scroll_x`. */

#include "safec/tslice.h"

typedef struct {
    uint64_t rows;
    uint32_t row_h, header_h;
    uint32_t view_w, view_h;
    uint64_t scroll_y;
    uint64_t scroll_x;
    uint64_t total_w; /* the columns' summed widths */
} grid_geom;

/* The body's height: view_h less the header, 0 when the header fills it. */
uint32_t grid_body_h(grid_geom g);

/* The furthest scroll that still fills the body: the last full page. */
uint64_t grid_max_scroll_y(grid_geom g);
uint64_t grid_max_scroll_x(grid_geom g);

/* Clamps both scrolls. */
grid_geom grid_clamp(grid_geom g);

/* The rows the body shows, whole or in part: `count` rows from `first`, the
 * first one's top at `top` points from the body's top (0 or negative). */
typedef struct {
    uint64_t first, count;
    int64_t top;
} grid_rows;

grid_rows grid_visible(grid_geom g);

/* Rows that fit the body whole (at least 1): the page for Page Up/Down. */
uint64_t grid_page(grid_geom g);

/* The row under `y`, points from the region's top. False on the header,
 * below the last row, or outside the region. */
bool grid_row_at(grid_geom g, int64_t y, uint64_t *row);

/* The smallest scroll that shows `row` whole, from the current one. */
uint64_t grid_reveal(grid_geom g, uint64_t row);

/* The columns that show: [first, end) of `n` widths (uint32_t in `a`), and
 * the first one's left edge, scroll applied (0 or negative: clipped, not
 * dropped). False on a bad slice. */
typedef struct {
    size_t first, end;
    int64_t left;
} grid_cols;

bool grid_columns(grid_geom g, const sc_arena *a, sc_tslice widths, size_t n, grid_cols *out);

/* What a key asks of the grid when it has focus. */
typedef enum {
    GRID_KEY_NONE,
    GRID_KEY_UP,
    GRID_KEY_DOWN,
    GRID_KEY_PAGE_UP,
    GRID_KEY_PAGE_DOWN,
    GRID_KEY_FIRST,  /* ⌘↑ */
    GRID_KEY_LAST,   /* ⌘↓ */
    GRID_KEY_CLEAR,   /* Esc */
    GRID_KEY_ALL
} grid_key;

/* The request for SDL keycode `key` with UI_MOD_* bits `mods` on `platform`
 * (UI_PLATFORM_*). ⌘ is UI_MOD_CMD; on the Mac, Ctrl is a modifier of its
 * own and makes no grid key (KTD26). */
grid_key grid_key_for(uint32_t key, uint16_t mods, uint8_t platform);

/* The scroll and the selection. */
typedef struct {
    grid_geom g;
    bool selected;
    uint64_t row;
    uint64_t anchor;
    bool extend;
} grid_sel;

bool grid_contains(const grid_sel *s, uint64_t row);

/* Applies a key: moves or clears the selection, scrolling the least that
 * keeps the selected row whole. True when anything changed. A move with no
 * selection starts at the first visible row. */
bool grid_move(grid_sel *s, grid_key k);

/* Selects the row under `y` (points from the region's top). */
bool grid_click(grid_sel *s, int64_t y);

/* Scrolls by a wheel delta in SDL's sense, `dx` right and `dy` up when
 * positive, then clamps. True when the scroll moved. */
bool grid_wheel(grid_sel *s, int32_t dx, int32_t dy);

/* Widens column `c` of `widths` (uint32_t in `a`) to `need` points, at most
 * `max`, adding the growth to *total. Never narrows. True when it grew. */
bool grid_widen(sc_arena *a, sc_tslice widths, size_t c, uint32_t need, uint32_t max,
                uint64_t *total);

#endif
