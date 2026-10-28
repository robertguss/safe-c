#ifndef EXPLORER_PANES_H
#define EXPLORER_PANES_H

#include "frame.h"

#define PANES_TAG_SIDEBAR 0x411u
#define PANES_TAG_EDITOR 0x412u

typedef struct {
    uint32_t sidebar, editor;
    uint32_t drag;
    int32_t offset;
} pane_state;

uint32_t panes_sidebar_width(uint32_t requested, uint32_t window);
uint32_t panes_editor_height(uint32_t requested, uint32_t preferred, uint32_t room);
uint32_t panes_hit(const ui_layout *l, int32_t x, int32_t y);
uint8_t panes_cursor(const pane_state *p, const ui_layout *l, int32_t x, int32_t y);
bool panes_event(pane_state *p, const ui_layout *l, ui_event ev);

#endif
