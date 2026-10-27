/* Tests that the Postico shell (examples/explorer/view_shell.c) builds a frame
 * the adapter accepts in every theme, status and layout. Core code, built in. */
#include "../examples/explorer/frame.c"
#include "../examples/explorer/theme.c"
#include "../examples/explorer/view_shell.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>

#define ARENA_BYTES (1u << 18)

static alignas(8) uint8_t storage[ARENA_BYTES];
static sc_arena a;

static size_t build(bool dark, uint8_t status, bool sidebar, uint8_t reconnect) {
    frame_builder b;
    ui_frame f;
    shell_state s;
    assert(sc_arena_reset(&a));
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
    assert(frame_begin(&b, &a, 256, 0));
    assert(shell_build(&b, &s));
    assert(frame_end(&b, &f));
    assert(ui_frame_valid(&a, f));
    return f.count;
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
    }
    puts("explorer_shell_test: ok");
    return 0;
}
