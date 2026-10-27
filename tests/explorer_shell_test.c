/* Tests that the Postico shell (examples/explorer/view_shell.c) builds a frame
 * the adapter accepts in every theme, status and layout. Core code, built in. */
#include "../examples/explorer/frame.c"
#include "../examples/explorer/theme.c"
#include "../examples/explorer/view_shell.c"
#include "../examples/explorer/focus.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>

#define ARENA_BYTES (1u << 18)

static alignas(8) uint8_t storage[ARENA_BYTES];
static sc_arena a;

static shell_tables caption(void) {
    shell_tables t = {SHELL_TABLES_CAPTION, NULL, {{NULL, 0, 0}, 0}, 0, {NULL, 0, 0}, 0, {NULL, 0, 0}};
    return t;
}

static ui_frame build_with(bool dark, uint8_t status, bool sidebar, uint8_t reconnect,
                           shell_tables tables) {
    frame_builder b;
    ui_frame f;
    shell_state s;
    s.dark = dark;
    s.show_sidebar = sidebar;
    s.object = SC_STR_LIT(&a, "public.users");
    s.server = SC_STR_LIT(&a, "localhost");
    s.connection = SHELL_CONNECTED;
    s.connection_label = SC_STR_LIT(&a, "Connected");
    s.reconnect = reconnect;
    s.reconnect_label = SC_STR_LIT(&a, "Reconnect");
    s.result_label = SC_STR_LIT(&a, "Query Result");
    s.result_count = SC_STR_LIT(&a, "3 rows");
    s.status = status;
    s.elapsed = SC_STR_LIT(&a, "12 ms");
    s.placeholder = SC_STR_LIT(&a, "");
    s.strip = NULL;
    s.strip_ctx = NULL;
    s.tables = tables;
    assert(frame_begin(&b, &a, 256, 0));
    assert(shell_build(&b, &s));
    assert(frame_end(&b, &f));
    assert(ui_frame_valid(&a, f));
    return f;
}

static size_t build(bool dark, uint8_t status, bool sidebar, uint8_t reconnect) {
    assert(sc_arena_reset(&a));
    return build_with(dark, status, sidebar, reconnect, caption()).count;
}

static size_t retry_tags(ui_frame f) {
    size_t n = 0;
    for (size_t i = 0; i < f.count; i++) {
        ui_node node;
        assert(SC_TSLICE_GET(ui_node, &a, f.nodes, i, &node));
        n += node.tag == FOCUS_TAG_META_RETRY ? 1u : 0u;
    }
    return n;
}

static void tables_box(bool dark) {
    assert(sc_arena_reset(&a));
    shell_tables t = caption();
    t.names_arena = &a;
    t.names = SC_TSLICE_ALLOC(sc_str, &a, 2);
    sc_str n0 = SC_STR_LIT(&a, "My Schema"), n1 = SC_STR_LIT(&a, "public");
    assert(SC_TSLICE_SET(sc_str, &a, t.names, 0, &n0) && SC_TSLICE_SET(sc_str, &a, t.names, 1, &n1));
    t.message = SC_STR_LIT(&a, "The metadata connection was lost.");
    t.retry_label = SC_STR_LIT(&a, "Retry");
    size_t base = build_with(dark, SHELL_STATUS_NONE, true, SHELL_RECONNECT_NONE, caption()).count;
    t.status = SHELL_TABLES_LOADING;
    assert(build_with(dark, SHELL_STATUS_NONE, true, SHELL_RECONNECT_NONE, t).count == base);
    t.count = 2;
    t.status = SHELL_TABLES_READY;
    ui_frame f = build_with(dark, SHELL_STATUS_NONE, true, SHELL_RECONNECT_NONE, t);
    assert(f.count == base - 1u + 4u && retry_tags(f) == 0);
    t.status = SHELL_TABLES_ERROR;
    t.retry = SHELL_RECONNECT_ENABLED;
    f = build_with(dark, SHELL_STATUS_NONE, true, SHELL_RECONNECT_NONE, t);
    assert(f.count == base + 4u + 3u && retry_tags(f) == 1);
    t.retry = SHELL_RECONNECT_WAITING;
    f = build_with(dark, SHELL_STATUS_NONE, true, SHELL_RECONNECT_NONE, t);
    assert(f.count == base + 4u + 3u && retry_tags(f) == 0);
    t.status = SHELL_TABLES_DISCONNECTED;
    t.retry = SHELL_RECONNECT_ENABLED;
    f = build_with(dark, SHELL_STATUS_NONE, true, SHELL_RECONNECT_NONE, t);
    assert(f.count == base + 4u && retry_tags(f) == 0);
}

int main(void) {
    sc_arena_init(&a, storage, ARENA_BYTES);
    for (int dark = 0; dark < 2; dark++) {
        size_t none = build(dark, SHELL_STATUS_NONE, true, SHELL_RECONNECT_NONE);
        /* A finished query adds the mark and the time on the right. */
        assert(build(dark, SHELL_STATUS_OK, true, SHELL_RECONNECT_NONE) == none + 2);
        assert(build(dark, SHELL_STATUS_ERROR, true, SHELL_RECONNECT_NONE) == none + 2);
        assert(build(dark, SHELL_STATUS_NONE, false, SHELL_RECONNECT_NONE) < none);
        /* A Reconnect button, enabled or waiting, adds its box and label (U13). */
        assert(build(dark, SHELL_STATUS_NONE, true, SHELL_RECONNECT_ENABLED) == none + 2);
        assert(build(dark, SHELL_STATUS_NONE, true, SHELL_RECONNECT_WAITING) == none + 2);
        tables_box(dark);
    }
    puts("explorer_shell_test: ok");
    return 0;
}
