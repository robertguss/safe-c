#ifndef EXPLORER_VIEW_TABLE_H
#define EXPLORER_VIEW_TABLE_H

/* A table view (plan U20): a relation's Content, a page of its rows in the
 * query view's grid (grid_result_area), with a note over it when the
 * relation has no row order; or its Structure, a read-only virtualised list
 * of its columns with their chips, its indexes and its check and foreign-key
 * constraints, every column listed. Postico's bottom bar goes under either:
 * the blue Content | Structure switch, the row count, and the pager. Glue
 * code: grid_build calls it when the tab shows a table view. The switch's
 * halves, the pager's ‹ › and the Structure list are tagged (focus.h). */

#include "view_grid.h"

#define TABLE_STRUCTURE_ROW 23u

/* The points a table view takes above its rows: the note, when shown. */
uint32_t table_chrome(const grid_view *v);

/* The toolbar title: "schema.table", in the frame arena. */
sc_str table_title(frame_builder *b, const grid_view *v);

/* The centre view: Content or Structure. */
bool table_centre(frame_builder *b, const grid_view *v);

/* shell_strip_fn for the bottom bar; `ctx` is the grid_view. */
bool table_bottom(frame_builder *b, const void *ctx);

#endif
