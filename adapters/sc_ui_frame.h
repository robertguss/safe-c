#ifndef SAFEC_ADAPTER_UI_FRAME_H
#define SAFEC_ADAPTER_UI_FRAME_H

/* The frame vocabulary the explorer hands the UI adapter (adapters/ui.c): types
 * only, so the app's core frame builder and its tests can include this header
 * and still link against the stdlib alone (plan KTD4).
 *
 * A frame is a list of nodes in an arena. Each node names its parent by index;
 * a parent always comes earlier in the list and is a box, and the nodes after a
 * box up to the end of its subtree are its descendants. That is the order a
 * depth-first walk visits them, so the adapter rebuilds the tree with no
 * recursion and no stack. Coordinates and sizes are integer points; the
 * adapter scales them for HiDPI displays (KTD8).
 *
 * A custom node is a leaf that Clay sizes like a box; its contents are a run
 * of ui_draw_item records the app positions itself, relative to the node's top-left
 * corner and clipped to it (KTD3: the result grid is drawn this way). */

#include "safec/str.h"
#include "safec/tslice.h"

#define UI_NO_PARENT UINT32_MAX

/* Fonts the adapter loads from fonts/ next to the binary. */
enum { UI_FONT_SANS = 0, UI_FONT_SANS_MEDIUM = 1, UI_FONT_MONO = 2, UI_FONT_COUNT = 3 };

enum { UI_NODE_BOX = 0, UI_NODE_TEXT = 1, UI_NODE_CUSTOM = 2 };
/* RECT fills x, y, w, h. LINE runs one point wide from (x, y) to (x + w, y + h).
 * TEXT draws one line of text with its top-left corner at (x, y); when w and h
 * are both above 0 it is also clipped to that box, so stacked marks or a wide
 * glyph cannot spill into a neighbour. */
enum { UI_DRAW_RECT = 0, UI_DRAW_LINE = 1, UI_DRAW_TEXT = 2 };
enum { UI_ROW = 0, UI_COLUMN = 1 };
enum { UI_ALIGN_START = 0, UI_ALIGN_END = 1, UI_ALIGN_CENTER = 2 };
/* FIT shrinks to the children, GROW takes the space left in the parent, and
 * FIXED is exactly `min` points. `max` of 0 means unbounded for FIT and GROW. */
enum { UI_SIZE_FIT = 0, UI_SIZE_GROW = 1, UI_SIZE_FIXED = 2 };

typedef struct {
    uint8_t r, g, b, a;
} ui_color;

typedef struct {
    uint8_t mode;
    uint16_t min, max;
} ui_size;

typedef struct {
    uint32_t parent; /* index of an earlier box, or UI_NO_PARENT */
    uint8_t kind;
    uint8_t direction;
    uint8_t align_x, align_y;
    ui_size width, height;
    uint16_t pad_left, pad_right, pad_top, pad_bottom;
    uint16_t gap;
    uint16_t radius;
    uint16_t border;
    ui_color border_color;
    ui_color background; /* alpha 0 draws nothing */
    /* UI_NODE_TEXT only. The bytes live in the frame arena until the adapter
     * has drawn the frame; the adapter never copies them before drawing.
     * Text is one line: it does not wrap. */
    sc_str text;
    uint8_t font;
    uint16_t font_size;
    ui_color text_color;
    /* UI_NODE_CUSTOM only: the node draws records draw_first ..
     * draw_first + draw_count - 1 of the frame's draw list. */
    uint32_t draw_first, draw_count;
} ui_node;

typedef struct {
    uint8_t kind;
    int32_t x, y, w, h;
    ui_color color;
    sc_str text; /* UI_DRAW_TEXT: lives in the frame arena, like a node's */
    uint8_t font;
    uint16_t font_size;
} ui_draw_item;

typedef struct {
    sc_tslice nodes; /* ui_node elements; only the first `count` are used */
    size_t count;
    sc_tslice draws; /* ui_draw_item elements; only the first `draw_count` are used */
    size_t draw_count;
} ui_frame;

enum {
    UI_EVENT_NONE = 0,
    UI_EVENT_QUIT,
    UI_EVENT_KEY_DOWN,
    UI_EVENT_TEXT,
    UI_EVENT_POINTER_MOVE,
    UI_EVENT_POINTER_DOWN,
    UI_EVENT_POINTER_UP,
    UI_EVENT_WHEEL,
    UI_EVENT_RESIZE,
    UI_EVENT_THEME,
    UI_EVENT_EXPOSE, /* the window must be redrawn */
    UI_EVENT_WAKE
};

enum { UI_MOD_SHIFT = 1, UI_MOD_CTRL = 2, UI_MOD_ALT = 4, UI_MOD_GUI = 8 };

/* The SDL keycodes the app reads. A letter key is its lower-case ASCII code. */
#define UI_KEY_BACKSPACE 0x08u
#define UI_KEY_TAB 0x09u
#define UI_KEY_RETURN 0x0du
#define UI_KEY_ESCAPE 0x1bu
#define UI_KEY_PAGE_UP 0x4000004bu
#define UI_KEY_PAGE_DOWN 0x4000004eu
#define UI_KEY_RIGHT 0x4000004fu
#define UI_KEY_LEFT 0x40000050u
#define UI_KEY_DOWN 0x40000051u
#define UI_KEY_UP 0x40000052u
#define UI_KEY_KP_ENTER 0x40000058u

typedef struct {
    uint8_t kind;
    uint32_t key;    /* KEY_DOWN: the SDL keycode */
    uint16_t mods;   /* KEY_DOWN and pointer events: UI_MOD_* bits */
    sc_str text;     /* TEXT: UTF-8 bytes, in the arena passed to ui_wait */
    int32_t x, y;    /* pointer events: position in points */
    int32_t wheel_x, wheel_y; /* WHEEL: scroll in points */
    uint32_t width, height;   /* RESIZE: new window size in points */
    bool dark;       /* THEME: the system theme is now dark */
} ui_event;

#endif
