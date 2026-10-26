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
#include "view_shell.h"

#define GRID_MAX_ROWS 200u
#define GRID_MAX_COLUMNS 32u

/* A note under the input box about the last key or command. */
enum {
    GRID_NOTICE_NONE = 0,
    GRID_NOTICE_TOO_BIG,       /* a paste or key would overflow the input box */
    GRID_NOTICE_NOT_ASCII,     /* a paste or key held a byte other than ASCII */
    GRID_NOTICE_CLIP_TOO_BIG,  /* the clipboard holds more than the input box */
    GRID_NOTICE_BUSY,          /* run refused: a query is active */
    GRID_NOTICE_NOT_CONNECTED, /* run refused: no connection */
    GRID_NOTICE_POOL           /* run refused: no result arena free */
};

typedef struct {
    bool dark;
    const conn_link *link;
    const input_box *input;
    bool custom_conninfo; /* --conninfo was given, rather than the PG* environment */
    uint64_t elapsed_ms;  /* of the last run, or of the active one so far */
    bool ran;             /* a run has been sent */
    uint8_t notice;
    uint32_t height;      /* the window's height in points */
} grid_view;

/* Builds the whole window into `b`. */
bool grid_build(frame_builder *b, const grid_view *v);

#endif
