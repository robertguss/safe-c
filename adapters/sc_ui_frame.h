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
 * corner and clipped to it (KTD3: the result grid is drawn this way). Its own
 * background, if any, is drawn first, under the records.
 *
 * A box or custom node may carry a nonzero tag, unique within its frame. After
 * layout, ui_draw reports each tagged node's rectangle back to the app
 * (ui_layout), so the next input event is hit-tested against where the node
 * was actually drawn (plan KTD18). */

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
    /* Boxes and custom nodes: nonzero to have ui_draw report the node's
     * rectangle; unique within the frame. A text node's tag must be 0. */
    uint32_t tag;
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

/* A tagged node's laid-out rectangle, in integer points from the window's
 * top-left corner. */
typedef struct {
    uint32_t tag;
    int32_t x, y, w, h;
} ui_rect;

/* Where ui_draw reports the tagged nodes of the last frame it drew: ui_rect
 * elements in `arena`, in node order (so a node comes after the boxes that
 * hold it), of which the first `count` are used. */
typedef struct {
    sc_arena *arena;
    sc_tslice rects;
    size_t count;
} ui_layout;

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
    UI_EVENT_WAKE,
    UI_EVENT_COMPOSE /* the IME's uncommitted text changed; empty text ends it */
};

/* UI_MOD_CMD is the platform's command modifier: ⌘ on macOS, Ctrl elsewhere.
 * UI_MOD_CTRL is the Control key itself on every platform, so on Linux Ctrl
 * sets both (plan KTD26). */
enum { UI_MOD_SHIFT = 1, UI_MOD_CTRL = 2, UI_MOD_ALT = 4, UI_MOD_CMD = 8, UI_MOD_GUI = 16 };

/* What ui_platform reports, for the key maps' Ctrl rules. */
enum { UI_PLATFORM_OTHER = 0, UI_PLATFORM_MAC = 1 };

/* Pointer buttons, as SDL numbers them; any other button is 0. */
enum { UI_BUTTON_LEFT = 1, UI_BUTTON_MIDDLE = 2, UI_BUTTON_RIGHT = 3 };

/* The SDL keycodes the app reads. A letter key is its lower-case ASCII code. */
#define UI_KEY_BACKSPACE 0x08u
#define UI_KEY_TAB 0x09u
#define UI_KEY_RETURN 0x0du
#define UI_KEY_ESCAPE 0x1bu
#define UI_KEY_DELETE 0x7fu
#define UI_KEY_HOME 0x4000004au
#define UI_KEY_PAGE_UP 0x4000004bu
#define UI_KEY_END 0x4000004du
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
    bool repeat;     /* KEY_DOWN: an auto-repeat of a held key */
    uint8_t button;  /* POINTER_DOWN and POINTER_UP: UI_BUTTON_* */
    uint8_t clicks;  /* POINTER_DOWN and POINTER_UP: 1 for a single click, 2 for a double */
    sc_str text;     /* TEXT and COMPOSE: UTF-8 bytes, in the arena passed to ui_wait */
    size_t compose_start, compose_len; /* COMPOSE: the IME's selection in `text`, in bytes;
                                          a caret at compose_start when compose_len is 0 */
    int32_t x, y;    /* pointer and WHEEL events: position in points */
    int32_t wheel_x, wheel_y; /* WHEEL: scroll in points, SDL's sense: x right, y up */
    uint32_t width, height;   /* RESIZE: new window size in points */
    bool dark;       /* THEME: the system theme is now dark */
} ui_event;

#endif
