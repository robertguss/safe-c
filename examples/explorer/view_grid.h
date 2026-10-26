#ifndef EXPLORER_VIEW_GRID_H
#define EXPLORER_VIEW_GRID_H

/* The query view (plans U8 and U11): the SQL input box over the result of the
 * last run, in the Postico shell. Glue code: it reads the result through
 * link.h (result.h) and measures text through the UI adapter.
 *
 * The grid is virtualised (grid.h): each frame reads and lays out only the
 * rows and columns the viewport shows, so a million-row result costs what a
 * screenful does. Column widths are measured once per result set, from its
 * header and first chunk, and kept in grid_state. Every string the frame holds
 * is a display copy in the frame arena. */

#include "grid.h"
#include "input.h"
#include "link.h"
#include "query_text.h"

#define GRID_MAX_COLUMNS 128u /* columns measured and drawn */
#define GRID_DETAIL_WIDTH 280u /* the row-detail pane */

/* The grid's state, which the app owns and the view keeps up to date: the
 * scroll and selection, the detail pane, where the grid sat in the window
 * last frame (for clicks), and the width cache. */
typedef struct {
    grid_sel sel;
    bool focus;    /* the grid has the keyboard, not the input box */
    bool detail;   /* the row-detail pane is open */
    int32_t x, y;  /* the grid region's top-left in the window, last frame */
    uint32_t w, h; /* and its size; 0 when no rows were shown */
    /* The width cache, for one result set. */
    sc_arena *wa;
    sc_tslice widths; /* uint32_t */
    size_t shown;
    uint64_t total;   /* the shown columns' summed widths */
    bool cached;      /* measured with the first chunk in hand */
    bool grew;        /* this frame widened a number column: build it again */
    uint64_t req, sets;
} grid_state;

/* A grid state whose width cache lives in `wa` (GRID_MAX_COLUMNS words). */
void grid_state_init(grid_state *g, sc_arena *wa);

typedef struct {
    bool dark;
    const conn_link *link;
    const input_box *input;
    bool custom_conninfo; /* --conninfo was given, rather than the PG* environment */
    uint64_t elapsed_ms;  /* of the last run, or of the active one so far */
    bool ran;             /* a run has been sent */
    uint8_t notice;       /* QUERY_NOTICE_* */
    uint32_t width, height; /* the window's size in points */
    grid_state *grid;     /* updated as the frame is built */
} grid_view;

/* Builds the whole window into `b`. */
bool grid_build(frame_builder *b, const grid_view *v);

#endif
