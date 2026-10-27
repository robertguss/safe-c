#include "../examples/explorer/frame.c"
#include "../examples/explorer/theme.c"
#include "../examples/explorer/grid.c"
#include "../examples/explorer/proto.c"
#include "../examples/explorer/result.c"
#include "../examples/explorer/catalog.c"
#include "../examples/explorer/tree.c"
#include "../examples/explorer/view_shell.c"
#include "../examples/explorer/budget.c"
#include "../examples/explorer/conn.c"
#include "../examples/explorer/query_text.c"
#include "../examples/explorer/view_text.c"
#include "../examples/explorer/view_sidebar.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

bool ui_text_size(const sc_arena *a, sc_str text, uint8_t font, uint16_t size, uint32_t *w,
                  uint32_t *h) {
    (void)a;
    (void)font;
    (void)size;
    *w = 7u * (uint32_t)text.len;
    *h = 16u;
    return true;
}

#define RELS 10000u

static alignas(8) uint8_t frame_mem[1u << 20], col_mem[1u << 12], chunk_mem[1u << 20],
    book_mem[1u << 16], rec_mem[4u << 20], keep_mem[1u << 12], tmp_mem[1u << 10];
static sc_arena fa, col_a, chunk_a, book, rec, keep, tmp;

static sc_str lit(sc_arena *a, const char *s) {
    size_t n = strlen(s);
    return n == 0 ? (sc_str){NULL, 0, a->epoch} : sc_str_from_lit(a, (const uint8_t *)s, n);
}

static char cell_buf[32];

static const char *cell(uint64_t cols, uint64_t r, uint64_t c) {
    static const char *const schemas[] = {"a", "b", "c"};
    if (cols == 2) {
        snprintf(cell_buf, sizeof cell_buf, "%llu", (unsigned long long)(100u + r));
        return c == 0 ? cell_buf : schemas[r];
    }
    if (c == 0 || c == 1) {
        snprintf(cell_buf, sizeof cell_buf, c == 0 ? "%llu" : "t%05llu", (unsigned long long)(1000u + r));
        return cell_buf;
    }
    return c == 2 ? &"rvmpf"[r % 5u] : "-1";
}

static result make(uint64_t cols, uint64_t rows) {
    result r;
    assert(sc_arena_reset(&book) && result_init(&r, &book, 4));
    assert(sc_arena_reset(&col_a) && sc_arena_reset(&chunk_a));
    proto_writer w;
    proto_header h = {PROTO_COLUMNS, 1, 1, 0, 0, 0};
    assert(proto_begin(&w, &col_a, h) && proto_put_u64(&w, cols));
    for (uint64_t c = 0; c < cols; c++) {
        assert(sc_arena_reset(&tmp) && proto_put_str(&w, &tmp, lit(&tmp, "c")) && proto_put_u64(&w, 25));
    }
    assert(proto_end(&w) && result_set_columns(&r, &col_a, 0) == RESULT_ADDED);
    if (rows == 0) {
        return r;
    }
    h.kind = PROTO_CHUNK;
    h.arg = rows;
    assert(proto_begin(&w, &chunk_a, h));
    for (uint64_t k = 0; k < rows * cols; k++) {
        const char *s = cell(cols, k / cols, k % cols);
        size_t n = cols == 4 && k % cols == 2 ? 1u : strlen(s);
        assert(sc_arena_reset(&tmp) && proto_put_u64(&w, n) &&
               proto_put_str(&w, &tmp, sc_str_from_lit(&tmp, (const uint8_t *)s, n)));
    }
    assert(proto_end(&w) && result_add_chunk(&r, &chunk_a, 1) == RESULT_ADDED);
    return r;
}

static void loaded(catalog *c) {
    assert(catalog_init(c, &rec, &keep) && catalog_restart(c));
    catalog_read t;
    result r = make(0, 0);
    r.has_columns = false;
    assert(catalog_next(c, &t) && catalog_take(c, t, &r));
    assert(catalog_next(c, &t) && catalog_next(c, &t) && t.kind == CATALOG_READ_SCHEMAS);
    r = make(2, 3);
    assert(catalog_take(c, t, &r));
    assert(catalog_expand(c, 1));
    catalog_read rr, fr;
    assert(catalog_next(c, &rr) && catalog_next(c, &fr));
    r = make(4, RELS);
    assert(catalog_take(c, rr, &r));
    r = make(5, 0);
    assert(catalog_take(c, fr, &r));
    assert(tree_count(c, TREE_TABLES) == 3u + RELS);
}

static ui_node node(ui_frame f, size_t i) {
    ui_node n;
    assert(SC_TSLICE_GET(ui_node, &fa, f.nodes, i, &n));
    return n;
}

static size_t find(ui_frame f, uint32_t tag) {
    for (size_t i = 0; i < f.count; i++) {
        if (node(f, i).tag == tag) {
            return i;
        }
    }
    return f.count;
}

static bool inside(ui_frame f, size_t i, size_t outer) {
    for (uint32_t p = node(f, i).parent; p != UI_NO_PARENT; p = node(f, p).parent) {
        if (p == outer) {
            return true;
        }
    }
    return false;
}

static ui_frame build(sidebar_view *v) {
    frame_builder b;
    ui_frame f;
    assert(sc_arena_reset(&fa));
    v->message = lit(&fa, "The metadata connection was lost.");
    v->retry_label = lit(&fa, "Retry");
    assert(frame_begin(&b, &fa, 256, 4096) && sidebar_build(&b, v) && frame_end(&b, &f));
    assert(ui_frame_valid(&fa, f));
    return f;
}

static size_t draws_of(ui_frame f, uint8_t kind, ui_color c, bool any_color) {
    ui_node rows = node(f, find(f, FOCUS_TAG_SIDEBAR_ROWS));
    size_t n = 0;
    for (uint32_t d = 0; d < rows.draw_count; d++) {
        ui_draw_item it;
        assert(SC_TSLICE_GET(ui_draw_item, &fa, f.draws, rows.draw_first + d, &it));
        n += it.kind == kind && (any_color || (it.color.r == c.r && it.color.g == c.g && it.color.b == c.b));
    }
    return n;
}

int main(void) {
    sc_arena_init(&fa, frame_mem, sizeof frame_mem);
    sc_arena_init(&col_a, col_mem, sizeof col_mem);
    sc_arena_init(&chunk_a, chunk_mem, sizeof chunk_mem);
    sc_arena_init(&book, book_mem, sizeof book_mem);
    sc_arena_init(&rec, rec_mem, sizeof rec_mem);
    sc_arena_init(&keep, keep_mem, sizeof keep_mem);
    sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
    catalog c;
    loaded(&c);
    tree_state t;
    tree_init(&t);
    sidebar_view v = {0};
    v.status = SIDEBAR_READY;
    v.cat = &c;
    v.tree = &t;
    v.focused = true;
    v.can_reload = true;
    v.height = 900;
    t.g.scroll_y = 5000u * THEME_SIDEBAR_ROW_HEIGHT;
    t.selected = true;
    t.sel.kind = TREE_ROW_RELATION;
    t.sel.schema = 1;
    t.sel.child = 5000;
    ui_frame f = build(&v);
    size_t side = find(f, FOCUS_TAG_SIDEBAR), rows = find(f, FOCUS_TAG_SIDEBAR_ROWS);
    assert(side < f.count && rows < f.count && inside(f, rows, side));
    assert(node(f, rows).kind == UI_NODE_CUSTOM && node(f, rows).height.min == sidebar_rows_height(&v));
    assert(inside(f, find(f, FOCUS_TAG_SIDEBAR_TABLES), side) && inside(f, find(f, FOCUS_TAG_SIDEBAR_FUNCTIONS), side));
    assert(inside(f, find(f, FOCUS_TAG_SIDEBAR_RELOAD), side) && find(f, FOCUS_TAG_META_RETRY) == f.count);
    uint64_t shown = (sidebar_rows_height(&v) + THEME_SIDEBAR_ROW_HEIGHT - 1u) / THEME_SIDEBAR_ROW_HEIGHT;
    assert(draws_of(f, UI_DRAW_TEXT, theme_color(false, THEME_TEXT), false) == shown);
    assert(draws_of(f, UI_DRAW_RECT, theme_color(false, THEME_SIDEBAR_SELECTION), false) == 1);
    assert(node(f, rows).draw_count < shown * 12u);
    v.focused = false;
    f = build(&v);
    assert(draws_of(f, UI_DRAW_RECT, theme_color(false, THEME_SIDEBAR_SELECTION), false) == 0);
    assert(draws_of(f, UI_DRAW_RECT, theme_color(false, THEME_SIDEBAR_SELECTION_UNFOCUSED), false) == 1);
    v.dark = true;
    f = build(&v);
    assert(draws_of(f, UI_DRAW_RECT, theme_color(true, THEME_SIDEBAR_SELECTION_UNFOCUSED), false) == 1);
    v.dark = false;
    v.status = SIDEBAR_DISCONNECTED;
    v.can_reload = false;
    f = build(&v);
    assert(find(f, FOCUS_TAG_SIDEBAR_RELOAD) == f.count && find(f, FOCUS_TAG_SIDEBAR_ROWS) < f.count);
    assert(draws_of(f, UI_DRAW_TEXT, theme_color(false, THEME_TEXT), false) == 0);
    assert(node(f, find(f, FOCUS_TAG_SIDEBAR_ROWS)).height.min == sidebar_rows_height(&v));
    v.status = SIDEBAR_ERROR;
    v.retry = SHELL_RECONNECT_ENABLED;
    f = build(&v);
    assert(inside(f, find(f, FOCUS_TAG_META_RETRY), side));
    assert(node(f, find(f, FOCUS_TAG_SIDEBAR_ROWS)).height.min == sidebar_rows_height(&v));
    v.retry = SHELL_RECONNECT_WAITING;
    f = build(&v);
    assert(find(f, FOCUS_TAG_META_RETRY) == f.count);
    v.status = SIDEBAR_LOADING;
    f = build(&v);
    assert(find(f, FOCUS_TAG_SIDEBAR_ROWS) == f.count && find(f, FOCUS_TAG_SIDEBAR) < f.count);
    v.status = SIDEBAR_CAPTION;
    v.cat = NULL;
    v.tree = NULL;
    f = build(&v);
    assert(find(f, FOCUS_TAG_SIDEBAR_ROWS) == f.count && find(f, FOCUS_TAG_SIDEBAR_TABLES) < f.count);
    puts("explorer_view_sidebar_test: ok");
    return 0;
}
