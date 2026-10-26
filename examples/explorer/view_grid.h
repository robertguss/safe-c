#ifndef EXPLORER_VIEW_GRID_H
#define EXPLORER_VIEW_GRID_H

/* The query view (plan U8): the SQL input box over the result of the last run,
 * in the Postico shell. Glue code: it reads the result arenas through link.h
 * and measures text through the UI adapter.
 *
 * The rows are a simple list, not virtualised: at most GRID_MAX_ROWS rows and
 * GRID_MAX_COLUMNS columns, and only as many rows as the window can show. The
 * real grid comes in U11. Every string the frame holds is a display copy in the
 * frame arena, with control bytes shown as spaces. */

#include "input.h"
#include "link.h"
#include "query_text.h"

#define GRID_MAX_ROWS 200u
#define GRID_MAX_COLUMNS 32u

typedef struct {
    bool dark;
    const conn_link *link;
    const input_box *input;
    bool custom_conninfo; /* --conninfo was given, rather than the PG* environment */
    uint64_t elapsed_ms;  /* of the last run, or of the active one so far */
    bool ran;             /* a run has been sent */
    uint8_t notice;       /* QUERY_NOTICE_* */
    uint32_t width, height; /* the window's size in points */
} grid_view;

/* Builds the whole window into `b`. */
bool grid_build(frame_builder *b, const grid_view *v);

#endif
