#ifndef EXPLORER_FOCUS_H
#define EXPLORER_FOCUS_H

/* The focus model: which area has the keyboard, and where a key
 * goes. A key goes to the global table first (tabs, run, cancel, disconnect,
 * the detail pane, the sidebar), then to the focused area's own map, and otherwise nowhere. Text goes
 * only to the editor. A left click focuses the area it lands on, found by the
 * area's node tag in the last frame's layout, if that area has a key map. The
 * focused sidebar's map runs ahead of the global ⌘↩, which there opens the
 * selected object in a new tab rather than running the SQL.
 * Core code: no SDL. */

#include "grid.h"
#include "editor.h"
#include "tabs.h"
#include "tree.h"

typedef enum { FOCUS_EDITOR, FOCUS_GRID, FOCUS_DETAIL, FOCUS_SIDEBAR, FOCUS_PROMPT } focus_area;

#define FOCUS_TAG_EDITOR 1u
#define FOCUS_TAG_GRID 2u
#define FOCUS_TAG_DETAIL 3u
#define FOCUS_TAG_SIDEBAR 4u
#define FOCUS_TAG_PROMPT 5u

/* The query toolbar's buttons and the reconnect offers. A button is tagged
 * only while it is enabled, so a disabled one takes no click. */
#define FOCUS_TAG_EXECUTE 0x400u
#define FOCUS_TAG_CANCEL 0x401u
#define FOCUS_TAG_DISCONNECT 0x402u
#define FOCUS_TAG_RECONNECT 0x403u     /* in the centre view */
#define FOCUS_TAG_RECONNECT_BAR 0x404u /* beside the toolbar's connection state */
#define FOCUS_TAG_META_RETRY 0x405u
#define FOCUS_TAG_SIDEBAR_ROWS 0x406u
#define FOCUS_TAG_SIDEBAR_TABLES 0x407u
#define FOCUS_TAG_SIDEBAR_FUNCTIONS 0x408u
#define FOCUS_TAG_SIDEBAR_RELOAD 0x409u
#define FOCUS_TAG_NAV_BACK 0x40Au
#define FOCUS_TAG_NAV_FORWARD 0x40Bu
#define FOCUS_TAG_TABLE_CONTENT 0x40Cu
#define FOCUS_TAG_TABLE_STRUCTURE 0x40Du
#define FOCUS_TAG_PAGE_PREV 0x40Eu
#define FOCUS_TAG_PAGE_NEXT 0x40Fu
#define FOCUS_TAG_STRUCTURE 0x410u

typedef enum {
    FOCUS_TO_NONE,
    FOCUS_TO_TAB,    /* global: `tab` */
    FOCUS_TO_RUN,    /* global: ⌘↩ runs the selection or the statement at the cursor */
    FOCUS_TO_RUN_ALL, /* global: ⌥⌘↩ runs the whole editor */
    FOCUS_TO_CANCEL, /* global: ⌘. cancels the selected tab's run */
    FOCUS_TO_DISCONNECT, /* global: ⌥⌘. disconnects (Postico's shortcut) */
    FOCUS_TO_DETAIL, /* global: ⌥⌘0 shows or hides the row-detail pane */
    FOCUS_TO_EDITOR, /* the editor's map: `edit` */
    FOCUS_TO_GRID,   /* the grid's map: `grid` */
    FOCUS_TO_SIDEBAR,
    FOCUS_TO_TREE
} focus_to;

typedef struct {
    focus_to to;
    tab_key tab;
    editor_press edit;
    grid_key grid;
    tree_key tree;
} focus_key;

/* Where SDL keycode `key` with UI_MOD_* bits `mods` on `platform` goes while
 * `area` has focus. Esc is not global: in the grid it clears the selection. */
focus_key focus_route(focus_area area, uint32_t key, uint16_t mods, uint8_t platform);

/* Whether a text event goes to `area`. */
bool focus_takes_text(focus_area area);

/* The focus after a press of `button` on the node tagged `tag` (0: none):
 * the area it names on a left click when that area has a key map (the editor,
 * the grid, or the sidebar and its controls), else unchanged. */
focus_area focus_click(focus_area area, uint32_t tag, uint8_t button);

/* `area`, or the editor when the last frame `l` did not draw `area`: a hidden
 * grid or a closed pane gives the keyboard back. */
focus_area focus_shown(focus_area area, const ui_layout *l);

#endif
