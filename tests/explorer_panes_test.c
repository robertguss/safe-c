#include "../examples/explorer/frame.c"
#include "../examples/explorer/panes.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>

static alignas(8) uint8_t memory[4096];

static bool event(pane_state *p, const ui_layout *l, uint8_t kind, uint8_t button, int32_t x, int32_t y) {
    ui_event ev = {0};
    ev.kind = kind;
    ev.button = button;
    ev.x = x;
    ev.y = y;
    return panes_event(p, l, ev);
}

int main(void) {
    sc_arena a;
    sc_arena_init(&a, memory, sizeof memory);
    ui_rect rects[] = {{FOCUS_TAG_SIDEBAR, 4, 43, 220, 673},
                       {PANES_TAG_SIDEBAR, 224, 43, 4, 673},
                       {FOCUS_TAG_EDITOR, 229, 71, 866, 244},
                       {PANES_TAG_EDITOR, 229, 346, 866, 1}};
    ui_layout l = {&a, SC_TSLICE_ALLOC(ui_rect, &a, 4), 4};
    for (size_t i = 0; i < 4; i++) assert(SC_TSLICE_SET(ui_rect, &a, l.rects, i, &rects[i]));
    pane_state p = {0};
    assert(panes_sidebar_width(0, 1100) == 220);
    assert(panes_sidebar_width(179, 640) == 180);
    assert(panes_sidebar_width(181, 640) == 181);
    assert(panes_sidebar_width(305, 640) == 305);
    assert(panes_sidebar_width(307, 640) == 306);
    assert(panes_sidebar_width(UINT32_MAX, 640) == 306);
    assert(panes_editor_height(0, 244, 581) == 244);
    assert(panes_editor_height(79, 244, 581) == 80);
    assert(panes_editor_height(81, 244, 581) == 81);
    assert(panes_editor_height(484, 244, 581) == 484);
    assert(panes_editor_height(486, 244, 581) == 485);
    assert(panes_editor_height(UINT32_MAX, 244, 243) == 147);

    assert(panes_hit(&l, 223, 43) == PANES_TAG_SIDEBAR);
    assert(panes_hit(&l, 228, 715) == PANES_TAG_SIDEBAR);
    assert(panes_hit(&l, 222, 100) == 0 && panes_hit(&l, 229, 100) == 0);
    assert(panes_hit(&l, 226, 42) == 0 && panes_hit(&l, 226, 716) == 0);
    assert(panes_hit(&l, 229, 343) == PANES_TAG_EDITOR);
    assert(panes_hit(&l, 1094, 349) == PANES_TAG_EDITOR);
    assert(panes_hit(&l, 228, 346) != PANES_TAG_EDITOR && panes_hit(&l, 1095, 346) == 0);
    assert(panes_hit(&l, 600, 342) == 0 && panes_hit(&l, 600, 350) == 0);
    assert(panes_cursor(&p, &l, 226, 100) == UI_CURSOR_RESIZE_EW);
    assert(panes_cursor(&p, &l, 600, 346) == UI_CURSOR_RESIZE_NS);
    assert(panes_cursor(&p, &l, 600, 100) == UI_CURSOR_TEXT);
    assert(panes_cursor(&p, &l, 600, 330) == UI_CURSOR_DEFAULT);
    assert(!event(&p, &l, UI_EVENT_POINTER_DOWN, UI_BUTTON_RIGHT, 226, 100));
    assert(!event(&p, &l, UI_EVENT_POINTER_DOWN, UI_BUTTON_LEFT, 600, 330));
    assert(event(&p, &l, UI_EVENT_POINTER_DOWN, UI_BUTTON_LEFT, 227, 100));
    assert(event(&p, &l, UI_EVENT_POINTER_MOVE, 0, 227, 200) && p.sidebar == 220);
    assert(event(&p, &l, UI_EVENT_POINTER_MOVE, 0, 344, 200) && p.sidebar == 337);
    assert(panes_cursor(&p, &l, 600, 100) == UI_CURSOR_RESIZE_EW);
    assert(!event(&p, &l, UI_EVENT_POINTER_UP, UI_BUTTON_RIGHT, 600, 100));
    assert(event(&p, &l, UI_EVENT_POINTER_UP, UI_BUTTON_LEFT, 355, 200) && p.sidebar == 348);
    assert(panes_cursor(&p, &l, 600, 100) == UI_CURSOR_TEXT);
    assert(!event(&p, &l, UI_EVENT_POINTER_MOVE, 0, 500, 100) && p.sidebar == 348);

    assert(event(&p, &l, UI_EVENT_POINTER_DOWN, UI_BUTTON_LEFT, 600, 344));
    assert(event(&p, &l, UI_EVENT_POINTER_MOVE, 0, 700, 344) && p.editor == 244);
    assert(event(&p, &l, UI_EVENT_POINTER_MOVE, 0, 700, 293) && p.editor == 193);
    assert(panes_cursor(&p, &l, 100, 100) == UI_CURSOR_RESIZE_NS);
    assert(event(&p, &l, UI_EVENT_POINTER_UP, UI_BUTTON_LEFT, 700, 293) && p.editor == 193);
    assert(panes_cursor(&p, &l, 100, 100) == UI_CURSOR_DEFAULT);
    assert(event(&p, &l, UI_EVENT_POINTER_DOWN, UI_BUTTON_LEFT, 600, 346));
    assert(event(&p, &l, UI_EVENT_FOCUS_LOST, 0, 0, 0));
    assert(!event(&p, &l, UI_EVENT_POINTER_MOVE, 0, 600, 600) && p.editor == 193);
    assert(event(&p, &l, UI_EVENT_POINTER_DOWN, UI_BUTTON_LEFT, 600, 346));
    assert(event(&p, &l, UI_EVENT_RESIZE, 0, 0, 0));
    assert(!event(&p, &l, UI_EVENT_POINTER_MOVE, 0, 600, 600) && p.editor == 193);
    assert(event(&p, &l, UI_EVENT_POINTER_DOWN, UI_BUTTON_LEFT, 226, 100));
    l.count = 0;
    assert(event(&p, &l, UI_EVENT_POINTER_MOVE, 0, 600, 600));
    assert(panes_hit(&l, 226, 100) == 0 && panes_hit(&l, 600, 346) == 0);
    assert(panes_cursor(&p, &l, 226, 100) == UI_CURSOR_DEFAULT);
    puts("explorer_panes_test: ok");
    return 0;
}
