#ifndef EXPLORER_SCENE_SQL_H
#define EXPLORER_SCENE_SQL_H

/* The SQL each headless query scene (KTD15) types and runs once connected,
 * and the fixtures the sidebar and table scenes create. Glue code for
 * main.c's scene drivers: it includes only the stdlib. */

#include "safec/str.h"

/* What a scene runs once connected. */
enum { RUN_NONE = 0, RUN_ROWS, RUN_WIDE, RUN_TAG, RUN_ERROR, RUN_ERROR_ROWS, RUN_TERMINATE,
       RUN_GRID, RUN_NULL_WRAP, RUN_CAPPED, RUN_DETAIL, RUN_TABS, RUN_MILLION, RUN_CANCELLING,
       RUN_DISCONNECT_OFFER, RUN_RECONNECT, RUN_META_LOST, RUN_SIDEBAR, RUN_SIDEBAR_ERROR,
       RUN_TABLE_CONTENT, RUN_TABLE_STRUCTURE, RUN_TABLE_NOTE, RUN_TABLE_BACK, RUN_EDITOR,
       RUN_EDITOR_COMPOSE };

/* Scene `run`'s SQL (RUN_*), in `a`; empty for RUN_NONE. */
sc_str scene_sql(sc_arena *a, uint8_t run);

#endif
