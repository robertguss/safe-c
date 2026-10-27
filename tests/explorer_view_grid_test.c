/* Tests the node tags the query view (examples/explorer/view_grid.c) puts on
 * its frame, which every click is hit-tested against (plan U16, KTD18): each
 * tab, its close box inside it and the + carry their tab tags, and the
 * editor, grid, detail pane and sidebar carry their focus tags. The query
 * toolbar's Cancel (or Disconnect) and Execute, and the Reconnect offers,
 * carry theirs while enabled (U13). The caret
 * draws only while the editor has the keys. The sidebar holds its rows
 * region, its Tables | Functions switch and its reload button, and ⌘0's
 * hidden sidebar takes its tags with it (U19). Glue code, built in with the core
 * it calls; the adapter's text measure and the link's arena lookups are
 * stubbed, since make test links the stdlib alone. */
#include "../examples/explorer/frame.c"
#include "../examples/explorer/theme.c"
#include "../examples/explorer/grid.c"
#include "../examples/explorer/input.c"
#include "../examples/explorer/tabs.c"
#include "../examples/explorer/catalog.c"
#include "../examples/explorer/tree.c"
#include "../examples/explorer/focus.c"
#include "../examples/explorer/detail.c"
#include "../examples/explorer/proto.c"
#include "../examples/explorer/result.c"
#include "../examples/explorer/view_grid.h"
#include "../examples/explorer/view_shell.c"
#include "../examples/explorer/budget.c"
#include "../examples/explorer/conn.c"
#include "../examples/explorer/query_text.c"
#include "../examples/explorer/view_text.c"
#include "../examples/explorer/view_sidebar.c"
#include "../examples/explorer/view_grid.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

/* Seven points a byte, sixteen high. */
bool ui_text_size(const sc_arena *a, sc_str text, uint8_t font, uint16_t size, uint32_t *w,
                  uint32_t *h) {
    (void)a;
    (void)font;
    (void)size;
    *w = 7u * (uint32_t)text.len;
    *h = 16u;
    return true;
}

/* No terminal reply is kept and no connect failed, so neither is read. */
sc_arena *link_arena(const conn_link *l, size_t i) {
    (void)l;
    (void)i;
    return NULL;
}

const sc_arena *link_failure_arena(const conn_link *l) {
    (void)l;
    return NULL;
}

#define TABS 3u

static alignas(8) uint8_t frame_mem[1u << 20], col_mem[1u << 12], chunk_mem[1u << 12],
    book_mem[1u << 16], tmp_mem[1u << 10], wa_mem[1u << 12], scratch_mem[GRID_SCRATCH_BYTES],
    input_mem[1u << 12];
static sc_arena fa, col_a, chunk_a, book, tmp, wa, scratch, input_a;
static conn_link link; /* static: zeroed, and too big for the stack */

static sc_str lit(sc_arena *a, const char *s) {
    return sc_str_from_lit(a, (const uint8_t *)s, strlen(s));
}

/* Two columns, an integer and a text, and three rows of short values. */
static void fill(result *r) {
    proto_writer w;
    proto_header h = {PROTO_COLUMNS, 1, 1, 0, 0, 0};
    assert(proto_begin(&w, &col_a, h) && proto_put_u64(&w, 2) &&
           proto_put_str(&w, &tmp, lit(&tmp, "id")) && proto_put_u64(&w, 23) &&
           proto_put_str(&w, &tmp, lit(&tmp, "name")) && proto_put_u64(&w, 25) && proto_end(&w));
    proto_header ch = {PROTO_CHUNK, 1, 1, 3, 0, 0};
    assert(proto_begin(&w, &chunk_a, ch));
    for (int k = 0; k < 6; k++) {
        sc_str s = lit(&tmp, k % 2 == 0 ? "7" : "ada");
        assert(proto_put_u64(&w, s.len) && proto_put_str(&w, &tmp, s));
    }
    assert(proto_end(&w));
    assert(result_init(r, &book, 4) && result_set_columns(r, &col_a, 0) == RESULT_ADDED &&
           result_add_chunk(r, &chunk_a, 1) == RESULT_ADDED && r->rows == 3);
}

static ui_node node(ui_frame f, size_t i) {
    ui_node n;
    assert(SC_TSLICE_GET(ui_node, &fa, f.nodes, i, &n));
    return n;
}

/* The index of the one node tagged `tag`. */
static size_t tagged(ui_frame f, uint32_t tag) {
    size_t found = f.count;
    for (size_t i = 0; i < f.count; i++) {
        if (node(f, i).tag == tag) {
            assert(found == f.count);
            found = i;
        }
    }
    assert(found < f.count);
    return found;
}

static bool has_tag(ui_frame f, uint32_t tag) {
    for (size_t i = 0; i < f.count; i++) {
        if (node(f, i).tag == tag) {
            return true;
        }
    }
    return false;
}

static bool inside(ui_frame f, size_t i, size_t outer) {
    for (uint32_t p = node(f, i).parent; p != UI_NO_PARENT; p = node(f, p).parent) {
        if (p == outer) {
            return true;
        }
    }
    return false;
}

/* Draw lines in the editor's custom node: the caret is its only one. */
static size_t editor_lines(ui_frame f) {
    ui_node e = node(f, tagged(f, FOCUS_TAG_EDITOR));
    size_t lines = 0;
    for (uint32_t d = 0; d < e.draw_count; d++) {
        ui_draw_item it;
        assert(SC_TSLICE_GET(ui_draw_item, &fa, f.draws, e.draw_first + d, &it));
        lines += it.kind == UI_DRAW_LINE;
    }
    return lines;
}

static ui_frame build(grid_view *v) {
    frame_builder b;
    ui_frame f;
    assert(sc_arena_reset(&fa));
    assert(frame_begin(&b, &fa, 4096, 4096) && grid_build(&b, v) && frame_end(&b, &f));
    assert(ui_frame_valid(&fa, f));
    return f;
}

int main(void) {
    sc_arena_init(&fa, frame_mem, sizeof frame_mem);
    sc_arena_init(&col_a, col_mem, sizeof col_mem);
    sc_arena_init(&chunk_a, chunk_mem, sizeof chunk_mem);
    sc_arena_init(&book, book_mem, sizeof book_mem);
    sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
    sc_arena_init(&wa, wa_mem, sizeof wa_mem);
    sc_arena_init(&scratch, scratch_mem, sizeof scratch_mem);
    sc_arena_init(&input_a, input_mem, sizeof input_mem);
    result res;
    fill(&res);
    link.ui.state = UI_CONN_CONNECTED;
    conn_ui ui = link.ui;
    ui.q = Q_ROWS;
    ui.held_count = 2;
    ui.rows = 3;
    ui.sets = 1;
    grid_state g;
    grid_state_init(&g, &wa, &scratch);
    input_box in;
    assert(input_init(&in, &input_a, 256));
    assert(input_insert(&in, &input_a, lit(&input_a, "select 1")) == INPUT_OK);
    static alignas(8) uint8_t tabs_mem[1024];
    sc_arena tabs_a;
    sc_arena_init(&tabs_a, tabs_mem, sizeof tabs_mem);
    sc_tslice tabs = SC_TSLICE_ALLOC(view_tab, &tabs_a, TABS);
    for (size_t i = 0; i < TABS; i++) {
        view_tab t = {&tabs_a, lit(&tabs_a, "select 1"), i == 1, false};
        assert(SC_TSLICE_SET(view_tab, &tabs_a, tabs, i, &t));
    }
    grid_view v = {0};
    v.link = &link;
    v.conn_state = UI_CONN_CONNECTED;
    v.can_reconnect = true;
    v.stop = QUERY_STOP_DISABLED;
    v.ui = &ui;
    v.res = &res;
    v.res_req = 1;
    v.res_sets = 1;
    v.tabs_arena = &tabs_a;
    v.tabs = tabs;
    v.tab_count = TABS;
    v.tab_selected = 1;
    v.input = &in;
    v.editor_focused = true;
    v.ran = true;
    v.width = 1200;
    v.height = 800;
    v.grid = &g;
    static alignas(8) uint8_t cat_mem[1u << 12], keep_mem[1u << 10], defs_mem[1u << 10];
    sc_arena cat_a, keep_a, defs_a;
    sc_arena_init(&cat_a, cat_mem, sizeof cat_mem);
    sc_arena_init(&keep_a, keep_mem, sizeof keep_mem);
    sc_arena_init(&defs_a, defs_mem, sizeof defs_mem);
    catalog cat;
    assert(catalog_init(&cat, &cat_a, &keep_a, &defs_a));
    tree_state tree;
    tree_init(&tree);
    sidebar_view side = {0};
    side.status = SIDEBAR_READY;
    side.cat = &cat;
    side.tree = &tree;
    side.can_reload = true;
    side.height = 800;
    v.show_sidebar = true;
    v.sidebar = &side;
    ui_frame f = build(&v); /* lays out the grid and measures the widths */
    g.sel.selected = true;
    g.sel.row = 0;
    g.detail = true;
    f = build(&v);
    for (uint32_t i = 0; i < TABS; i++) {
        size_t tab = tagged(f, TAB_TAG_SELECT + i);
        size_t close = tagged(f, TAB_TAG_CLOSE + i);
        assert(inside(f, close, tab));
    }
    size_t plus = tagged(f, TAB_TAG_NEW);
    for (uint32_t i = 0; i < TABS; i++) {
        assert(!inside(f, plus, tagged(f, TAB_TAG_SELECT + i)));
    }
    size_t editor = tagged(f, FOCUS_TAG_EDITOR);
    size_t grid = tagged(f, FOCUS_TAG_GRID);
    size_t detail = tagged(f, FOCUS_TAG_DETAIL);
    size_t sidebar = tagged(f, FOCUS_TAG_SIDEBAR);
    assert(node(f, editor).kind == UI_NODE_CUSTOM && node(f, grid).kind == UI_NODE_CUSTOM);
    assert(!inside(f, grid, sidebar) && !inside(f, editor, sidebar) && !inside(f, detail, grid));
    /* The caret is drawn while the editor has the keys, and only then. */
    assert(editor_lines(f) == 1);
    v.editor_focused = false;
    f = build(&v);
    assert(editor_lines(f) == 0);

    /* The query toolbar sits between the SQL box and the grid: Execute is
     * always live; a greyed Cancel takes no click (U13). */
    size_t exec = tagged(f, FOCUS_TAG_EXECUTE);
    assert(tagged(f, FOCUS_TAG_EDITOR) < exec && exec < tagged(f, FOCUS_TAG_GRID));
    assert(!has_tag(f, FOCUS_TAG_CANCEL) && !has_tag(f, FOCUS_TAG_DISCONNECT));
    assert(!has_tag(f, FOCUS_TAG_RECONNECT) && !has_tag(f, FOCUS_TAG_RECONNECT_BAR));
    /* With a run to cancel, Cancel is immediately left of Execute. */
    v.stop = QUERY_STOP_CANCEL;
    f = build(&v);
    size_t stop = tagged(f, FOCUS_TAG_CANCEL);
    exec = tagged(f, FOCUS_TAG_EXECUTE);
    assert(node(f, stop).parent == node(f, exec).parent && stop < exec);
    for (size_t i = stop + 1u; i < exec; i++) {
        assert(node(f, i).parent != node(f, exec).parent); /* no sibling between them */
    }
    /* A cancel that hangs: Disconnect takes Cancel's place. */
    v.stop = QUERY_STOP_DISCONNECT;
    f = build(&v);
    stop = tagged(f, FOCUS_TAG_DISCONNECT);
    assert(!has_tag(f, FOCUS_TAG_CANCEL) && stop < tagged(f, FOCUS_TAG_EXECUTE));
    /* Disconnected, with the rows kept: Reconnect in the centre view and by
     * the toolbar's connection state, neither inside the grid. */
    v.stop = QUERY_STOP_DISABLED;
    v.conn_state = UI_CONN_DISCONNECTED;
    ui.q = Q_DISCONNECTED;
    f = build(&v);
    size_t centre = tagged(f, FOCUS_TAG_RECONNECT);
    size_t bar = tagged(f, FOCUS_TAG_RECONNECT_BAR);
    assert(!inside(f, centre, tagged(f, FOCUS_TAG_GRID)) && bar < centre);
    assert(has_tag(f, FOCUS_TAG_GRID)); /* the rows stay */
    /* Both retiring slots busy: the offers stay, greyed and untagged. */
    v.can_reconnect = false;
    f = build(&v);
    assert(!has_tag(f, FOCUS_TAG_RECONNECT) && !has_tag(f, FOCUS_TAG_RECONNECT_BAR));
    /* A failed connect: Reconnect under the message, and no rows. */
    v.can_reconnect = true;
    v.conn_state = UI_CONN_UNREACHABLE;
    f = build(&v);
    assert(has_tag(f, FOCUS_TAG_RECONNECT) && has_tag(f, FOCUS_TAG_RECONNECT_BAR));
    size_t side_box = tagged(f, FOCUS_TAG_SIDEBAR);
    assert(side_box < tagged(f, TAB_TAG_SELECT) && !inside(f, tagged(f, FOCUS_TAG_EDITOR), side_box));
    v.show_sidebar = false;
    f = build(&v);
    assert(!has_tag(f, FOCUS_TAG_SIDEBAR) && !has_tag(f, FOCUS_TAG_SIDEBAR_ROWS));
    assert(has_tag(f, FOCUS_TAG_EDITOR) && has_tag(f, TAB_TAG_NEW));
    puts("explorer_view_grid_test: ok");
    return 0;
}
