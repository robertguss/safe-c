#ifndef EXPLORER_FOCUS_H
#define EXPLORER_FOCUS_H

/* The focus model (plan U16): which area has the keyboard, and where a key
 * goes. A key goes to the global table first (tabs, run, cancel, the detail
 * pane), then to the focused area's own map, and otherwise nowhere. Text goes
 * only to the editor. A left click focuses the area it lands on, found by the
 * area's node tag in the last frame's layout (KTD18), if that area has a key
 * map. Core code: no SDL. */

#include "grid.h"
#include "input.h"
#include "tabs.h"

typedef enum { FOCUS_EDITOR, FOCUS_GRID, FOCUS_DETAIL, FOCUS_SIDEBAR, FOCUS_PROMPT } focus_area;

#define FOCUS_TAG_EDITOR 1u
#define FOCUS_TAG_GRID 2u
#define FOCUS_TAG_DETAIL 3u
#define FOCUS_TAG_SIDEBAR 4u
#define FOCUS_TAG_PROMPT 5u

typedef enum {
    FOCUS_TO_NONE,
    FOCUS_TO_TAB,    /* global: `tab` */
    FOCUS_TO_RUN,    /* global: ⌘↩ runs the selected tab */
    FOCUS_TO_CANCEL, /* global: Esc while the tab's query is active */
    FOCUS_TO_DETAIL, /* global: ⌥⌘0 shows or hides the row-detail pane */
    FOCUS_TO_EDITOR, /* the editor's map: `input` */
    FOCUS_TO_GRID    /* the grid's map: `grid` */
} focus_to;

typedef struct {
    focus_to to;
    tab_key tab;
    input_key input;
    grid_key grid;
} focus_key;

/* Where SDL keycode `key` with UI_MOD_* bits `mods` on `platform` goes while
 * `area` has focus; `query_active` says the selected tab's query runs. */
focus_key focus_route(focus_area area, uint32_t key, uint16_t mods, uint8_t platform,
                      bool query_active);

/* Whether a text event goes to `area`. */
bool focus_takes_text(focus_area area);

/* The focus after a press of `button` on the node tagged `tag` (0: none):
 * the area it names on a left click when that area has a key map (the editor
 * or the grid), else unchanged. */
focus_area focus_click(focus_area area, uint32_t tag, uint8_t button);

/* `area`, or the editor when the last frame `l` did not draw `area`: a hidden
 * grid or a closed pane gives the keyboard back. */
focus_area focus_shown(focus_area area, const ui_layout *l);

#endif
