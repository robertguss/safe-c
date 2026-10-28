#ifndef EXPLORER_VIEW_TABLE_H
#define EXPLORER_VIEW_TABLE_H

/* A table view: a relation's Content, a page of its rows in the query
 * view's grid (grid_result_area), with a note over it when the relation has
 * no row order; or its Structure, a read-only virtualised list of its
 * columns with their chips, its indexes and its check and foreign-key
 * constraints, every column listed. Postico's bottom bar goes under either:
 * the blue Content | Structure switch, the row count, and the pager. A page
 * the link refused shows why, and a relation that is gone says so. Glue
 * code: the app builds it instead of grid_build when the tab shows a table
 * view. The switch's halves, the pager's ‹ › and the Structure list are
 * tagged (focus.h). */

#include "nav.h"
#include "view_grid.h"

#define TABLE_STRUCTURE_ROW 23u

/* A table view of the tab: the window's grid_view, the stack entry shown,
 * and its relation's records when the catalog holds them (`found`), or
 * whether the relation is gone (page_gone). The pager's ‹ › are enabled
 * as `can_prev` and `can_next` say (nav_can_move). */
typedef struct {
    const grid_view *g;
    const nav_entry *entry;
    bool found, gone;
    catalog_schema schema;
    catalog_relation rel;
    bool can_prev, can_next;
} table_view;

/* Builds the whole window into `b` with the table view. */
bool table_build(frame_builder *b, const table_view *t);

#endif
