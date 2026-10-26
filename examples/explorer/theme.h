#ifndef EXPLORER_THEME_H
#define EXPLORER_THEME_H

/* Postico 2's palette and metrics (plan U3). Core code: integer RGBA tokens
 * and integer point metrics, no SDL. */

#include "../../adapters/sc_ui_frame.h"

/* Measured from Postico's screenshots (plan U3, step 1). */
enum {
    THEME_CONTENT = 0,
    THEME_ALT_ROW,
    THEME_GRID_HEADER,
    THEME_HEADER_LINE,
    THEME_COLUMN_LINE,
    THEME_ROW_SELECTION,
    THEME_ACCENT,
    THEME_SIDEBAR_BORDER,
    THEME_SIDEBAR_SELECTION,
    THEME_SIDEBAR_SELECTION_UNFOCUSED,
    THEME_BOTTOM_BAR,
    THEME_SECONDARY_TEXT,
    /* Chosen to sit with the measured ones, not measured: Postico's text,
     * window chrome and status colours are system colours. */
    THEME_TEXT,
    THEME_TOOLBAR,
    THEME_WINDOW,
    THEME_SELECTED_TEXT,
    THEME_SUCCESS,
    THEME_ERROR,
    THEME_TOKEN_COUNT
};

/* Points (plan U3, step 2). */
enum {
    THEME_GRID_HEADER_HEIGHT = 19,
    THEME_GRID_ROW_HEIGHT = 23,
    THEME_SIDEBAR_ROW_HEIGHT = 23,
    THEME_BOTTOM_BAR_HEIGHT = 30,
    THEME_TOOLBAR_HEIGHT = 38,
    THEME_BUTTON_HEIGHT = 20,
    THEME_BUTTON_RADIUS = 4,
    THEME_BOX_RADIUS = 6,
    THEME_INSET = 4,
    THEME_SIDEBAR_WIDTH = 220,
    THEME_TEXT_SIZE = 13,
    THEME_SMALL_TEXT_SIZE = 11,
    THEME_MAX_AUTO_COLUMN_WIDTH = 300
};

/* The token as 0xRRGGBB; 0 for an unknown token. */
uint32_t theme_hex(bool dark, uint32_t token);

/* The token as an opaque colour; black for an unknown token. */
ui_color theme_color(bool dark, uint32_t token);

#endif
