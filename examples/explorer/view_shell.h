#ifndef EXPLORER_VIEW_SHELL_H
#define EXPLORER_VIEW_SHELL_H

/* The Postico window chrome: toolbar, the two boxed sidebar panes,
 * the centre view and the query results bottom bar, built as frame nodes
 * from app state each frame. */

#include "frame.h"

enum { SHELL_OFFLINE = 0, SHELL_CONNECTING, SHELL_CONNECTED, SHELL_FAILED };
enum { SHELL_STATUS_NONE = 0, SHELL_STATUS_OK, SHELL_STATUS_ERROR };
/* The Reconnect button beside the connection state. */
enum { SHELL_RECONNECT_NONE = 0, SHELL_RECONNECT_ENABLED, SHELL_RECONNECT_WAITING };
/* A push button's look: grey, blue, or grey with greyed text. */
enum { SHELL_BUTTON_PLAIN = 0, SHELL_BUTTON_PRIMARY, SHELL_BUTTON_DISABLED };

/* Builds a band across the top of the content column, over the view, such
 * as the query tabs. */
typedef bool (*shell_strip_fn)(frame_builder *b, const void *ctx);

typedef struct {
    bool dark;
    bool show_sidebar;
    sc_str object;     /* toolbar title, top line: the open object */
    sc_str server;     /* toolbar title, bottom line */
    uint8_t connection;
    sc_str connection_label;
    uint8_t reconnect;   /* SHELL_RECONNECT_*: tagged FOCUS_TAG_RECONNECT_BAR while enabled */
    sc_str reconnect_label;
    sc_str result_label; /* bottom bar, left */
    sc_str result_count; /* bottom bar, centre */
    uint8_t status;      /* bottom bar, right: a tick or cross ... */
    sc_str elapsed;      /* ... and the elapsed time */
    sc_str placeholder;  /* centre view, until it has content */
    shell_strip_fn strip; /* NULL: no band over the view */
    const void *strip_ctx;
} shell_state;

/* A push button with `label` (live in `b`'s arena), tagged `tag` unless it is
 * disabled, so a disabled button takes no click. */
bool shell_button(frame_builder *b, bool dark, sc_str label, uint32_t tag, uint8_t style);

/* Builds the whole window into `b`, with `placeholder` in the centre view.
 * All strings live in `b`'s arena. */
bool shell_build(frame_builder *b, const shell_state *s);

/* The same window in two halves, for a centre view the caller builds:
 * shell_open leaves the centre box open (a column), and shell_close adds the
 * bottom bar and closes it and the window. */
bool shell_open(frame_builder *b, const shell_state *s);
bool shell_close(frame_builder *b, const shell_state *s);

#endif
