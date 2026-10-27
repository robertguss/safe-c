#include "grid.h"
#include "../../adapters/sc_ui_frame.h"

uint32_t grid_body_h(grid_geom g) {
    return g.view_h > g.header_h ? g.view_h - g.header_h : 0;
}

uint64_t grid_max_scroll_y(grid_geom g) {
    uint64_t body = grid_body_h(g);
    if (g.row_h == 0 || g.rows > UINT64_MAX / g.row_h) {
        return 0;
    }
    uint64_t all = g.rows * g.row_h;
    return all > body ? all - body : 0;
}

uint64_t grid_max_scroll_x(grid_geom g) {
    return g.total_w > g.view_w ? g.total_w - g.view_w : 0;
}

grid_geom grid_clamp(grid_geom g) {
    uint64_t my = grid_max_scroll_y(g), mx = grid_max_scroll_x(g);
    g.scroll_y = g.scroll_y < my ? g.scroll_y : my;
    g.scroll_x = g.scroll_x < mx ? g.scroll_x : mx;
    return g;
}

grid_rows grid_visible(grid_geom g) {
    grid_rows r = {0, 0, 0};
    uint64_t body = grid_body_h(g);
    g = grid_clamp(g);
    if (g.row_h == 0 || body == 0 || g.rows == 0) {
        return r;
    }
    r.first = g.scroll_y / g.row_h;
    r.top = -(int64_t)(g.scroll_y % g.row_h);
    uint64_t last = (g.scroll_y + body - 1u) / g.row_h; /* the row holding the body's last point */
    last = last < g.rows - 1u ? last : g.rows - 1u;
    r.count = r.first <= last ? last - r.first + 1u : 0;
    return r;
}

uint64_t grid_page(grid_geom g) {
    uint64_t n = g.row_h > 0 ? grid_body_h(g) / g.row_h : 0;
    return n > 0 ? n : 1u;
}

bool grid_row_at(grid_geom g, int64_t y, uint64_t *row) {
    g = grid_clamp(g);
    if (g.row_h == 0 || y < (int64_t)g.header_h || y >= (int64_t)g.view_h) {
        return false;
    }
    uint64_t r = (g.scroll_y + (uint64_t)(y - (int64_t)g.header_h)) / g.row_h;
    if (r >= g.rows) {
        return false;
    }
    *row = r;
    return true;
}

uint64_t grid_reveal(grid_geom g, uint64_t row) {
    g = grid_clamp(g);
    uint64_t body = grid_body_h(g);
    if (g.row_h == 0 || row >= g.rows) {
        return g.scroll_y;
    }
    uint64_t top = row * g.row_h, bottom = top + g.row_h;
    if (top < g.scroll_y || body < g.row_h) { /* a short body shows the row's top */
        uint64_t my = grid_max_scroll_y(g);
        return top < my ? top : my;
    }
    if (bottom > g.scroll_y + body) {
        uint64_t s = bottom > body ? bottom - body : 0;
        uint64_t my = grid_max_scroll_y(g);
        return s < my ? s : my;
    }
    return g.scroll_y;
}

bool grid_columns(grid_geom g, const sc_arena *a, sc_tslice widths, size_t n, grid_cols *out) {
    g = grid_clamp(g);
    grid_cols c = {n, n, 0};
    uint64_t x = 0; /* the left edge, unscrolled */
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        uint32_t w = 0;
        if (!SC_TSLICE_GET(uint32_t, a, widths, i, &w)) {
            return false;
        }
        if (!found && x + w > g.scroll_x) {
            found = true;
            c.first = i;
            c.left = (int64_t)x - (int64_t)g.scroll_x;
        }
        x += w;
        if (found && x >= g.scroll_x + g.view_w) {
            c.end = i + 1u;
            *out = c;
            return true;
        }
    }
    c.end = n;
    *out = c;
    return true;
}

grid_key grid_key_for(uint32_t key, uint16_t mods, uint8_t platform) {
    bool cmd = (mods & UI_MOD_CMD) != 0;
    bool mac_ctrl = platform == UI_PLATFORM_MAC && (mods & UI_MOD_CTRL) != 0;
    if ((mods & UI_MOD_ALT) != 0 || mac_ctrl) {
        return GRID_KEY_NONE;
    }
    switch (key) {
    case UI_KEY_UP:
        return cmd ? GRID_KEY_FIRST : GRID_KEY_UP;
    case UI_KEY_DOWN:
        return cmd ? GRID_KEY_LAST : GRID_KEY_DOWN;
    case UI_KEY_PAGE_UP:
        return cmd ? GRID_KEY_NONE : GRID_KEY_PAGE_UP;
    case UI_KEY_PAGE_DOWN:
        return cmd ? GRID_KEY_NONE : GRID_KEY_PAGE_DOWN;
    case UI_KEY_ESCAPE:
        return cmd ? GRID_KEY_NONE : GRID_KEY_CLEAR;
    default:
        return GRID_KEY_NONE;
    }
}

static bool select_row(grid_sel *s, uint64_t row) {
    bool changed = !s->selected || s->row != row;
    s->selected = true;
    s->row = row;
    uint64_t y = grid_reveal(s->g, row);
    changed = changed || y != s->g.scroll_y;
    s->g.scroll_y = y;
    return changed;
}

bool grid_move(grid_sel *s, grid_key k) {
    s->g = grid_clamp(s->g);
    if (k == GRID_KEY_CLEAR) {
        bool was = s->selected;
        s->selected = false;
        return was;
    }
    if (s->g.rows == 0 || k == GRID_KEY_NONE) {
        return false;
    }
    uint64_t last = s->g.rows - 1u;
    uint64_t at = s->selected && s->row <= last ? s->row : grid_visible(s->g).first;
    uint64_t page = grid_page(s->g);
    if (!s->selected && k != GRID_KEY_FIRST && k != GRID_KEY_LAST) {
        return select_row(s, at); /* the first move only selects */
    }
    switch (k) {
    case GRID_KEY_UP:
        at = at > 0 ? at - 1u : 0;
        break;
    case GRID_KEY_DOWN:
        at = at < last ? at + 1u : last;
        break;
    case GRID_KEY_PAGE_UP:
        at = at > page ? at - page : 0;
        break;
    case GRID_KEY_PAGE_DOWN:
        at = last - at > page ? at + page : last;
        break;
    case GRID_KEY_FIRST:
        at = 0;
        break;
    default: /* GRID_KEY_LAST */
        at = last;
        break;
    }
    return select_row(s, at);
}

bool grid_click(grid_sel *s, int64_t y) {
    uint64_t row = 0;
    s->g = grid_clamp(s->g);
    return grid_row_at(s->g, y, &row) && select_row(s, row);
}

static uint64_t step(uint64_t v, int32_t d, uint64_t max) {
    if (d > 0) {
        v = v > (uint64_t)d ? v - (uint64_t)d : 0;
    } else if (d < 0) {
        uint64_t up = (uint64_t)(-(int64_t)d);
        v = max - v > up ? v + up : max;
    }
    return v < max ? v : max;
}

bool grid_wheel(grid_sel *s, int32_t dx, int32_t dy) {
    grid_geom g = grid_clamp(s->g);
    uint64_t y = step(g.scroll_y, dy, grid_max_scroll_y(g));
    /* SDL's x is positive to the right, so the scroll grows with it. */
    uint64_t x = step(g.scroll_x, dx == INT32_MIN ? INT32_MAX : -dx, grid_max_scroll_x(g));
    bool moved = y != g.scroll_y || x != g.scroll_x;
    g.scroll_y = y;
    g.scroll_x = x;
    s->g = g;
    return moved;
}

bool grid_widen(sc_arena *a, sc_tslice widths, size_t c, uint32_t need, uint32_t max,
                uint64_t *total) {
    uint32_t w = 0;
    need = need < max ? need : max;
    if (!SC_TSLICE_GET(uint32_t, a, widths, c, &w) || need <= w ||
        !SC_TSLICE_SET(uint32_t, a, widths, c, &need)) {
        return false;
    }
    *total += need - w;
    return true;
}
