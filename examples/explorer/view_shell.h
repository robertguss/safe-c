#ifndef EXPLORER_VIEW_SHELL_H
#define EXPLORER_VIEW_SHELL_H

/* The Postico window chrome (plan U3): toolbar, the two boxed sidebar panes,
 * the centre view and the query results bottom bar, built as frame nodes
 * from app state each frame. */

#include "frame.h"

enum { SHELL_OFFLINE = 0, SHELL_CONNECTING, SHELL_CONNECTED, SHELL_FAILED };
enum { SHELL_STATUS_NONE = 0, SHELL_STATUS_OK, SHELL_STATUS_ERROR };

typedef struct {
    bool dark;
    bool show_sidebar; /* the right row-detail pane joins this in U11 */
    sc_str object;     /* toolbar title, top line: the open object */
    sc_str server;     /* toolbar title, bottom line */
    uint8_t connection;
    sc_str connection_label;
    sc_str result_label; /* bottom bar, left */
    sc_str result_count; /* bottom bar, centre */
    uint8_t status;      /* bottom bar, right: a tick or cross ... */
    sc_str elapsed;      /* ... and the elapsed time */
    sc_str placeholder;  /* centre view, until it has content */
} shell_state;

/* Builds the whole window into `b`. All strings live in `b`'s arena. */
bool shell_build(frame_builder *b, const shell_state *s);

#endif
