#ifndef EXPLORER_VIEW_EDITOR_H
#define EXPLORER_VIEW_EDITOR_H

/* The SQL editor's region: a line-number gutter, the visible lines
 * highlighted, the current statement's band, the selection, the composition
 * underlined at the cursor, and the caret. Glue code: it measures the mono
 * font through the UI adapter. The region is tagged FOCUS_TAG_EDITOR; its
 * geometry is shared with the hit test, so a click lands where the text was
 * drawn. */

#include "editor.h"
#include "frame.h"

#define VIEW_EDITOR_LINE_HEIGHT 18
#define VIEW_EDITOR_PAD 6

typedef struct {
    editor *ed;
    const sqlkw *kw;
    bool dark;
    bool focused;
    uint32_t width, height; /* the region, in points */
} editor_view;

/* The region's height for a centre column `centre` points tall: 40% of it,
 * and at least four lines. */
uint32_t view_editor_height(uint32_t centre);

/* Scrolls the cursor into view and draws the region into `b`. */
bool view_editor(frame_builder *b, const editor_view *v);

/* The offset under (x, y), in points from the region's top-left, measuring
 * in `a`. */
size_t view_editor_hit(sc_arena *a, const editor *e, int32_t x, int32_t y);

/* The caret's rectangle from the region's top-left, for ui_text_area: the
 * text area of the caret's line, and the caret `cursor` points right of
 * its left edge. False when the caret line is scrolled out. */
bool view_editor_caret(sc_arena *a, const editor *e, uint32_t width, uint32_t height, ui_rect *line,
                       int32_t *cursor);

#endif
