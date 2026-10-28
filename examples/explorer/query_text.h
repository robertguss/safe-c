#ifndef EXPLORER_QUERY_TEXT_H
#define EXPLORER_QUERY_TEXT_H

/* The words the query view shows (plan U8): display copies of result bytes,
 * the terminal reply's items, the toolbar's connection label, the bottom bar's
 * count or tag, and the notes under the editor. Core code: it includes only
 * the stdlib and other core headers, so tests/explorer_query_text_test.c links
 * it alone. view_grid.c lays these strings out. */

#include "conn.h"
#include "safec/utf8.h"
#include "view_shell.h"

/* A string being built in an arena. The first failure sticks. */
typedef struct {
    sc_arena *arena;
    sc_buf buf;
    bool failed;
} text_line;

/* Room for `cap` bytes in `a`. When `a` has no room, the line fails and
 * *lost is set: pass the frame builder's `failed` flag, so the frame is
 * refused rather than drawn with the text missing (plan U16). Every function
 * below that takes `lost` reports to it the same way. */
text_line text_new(sc_arena *a, size_t cap, bool *lost);
void text_lit(text_line *l, const uint8_t *lit, size_t n);
/* `"" lit` admits only a string literal, so sizeof is its length, never a pointer's. */
#define TEXT_LIT(l, lit) text_lit((l), (const uint8_t *)("" lit), sizeof("" lit) - 1)
/* Appends `s` (live in `sa`) for display, up to `max` bytes of output and
 * never part of a character: each control character (C0, DEL, C1) as a space,
 * since a frame's text is one line, and each byte that is not valid UTF-8 as
 * U+FFFD. It keeps at most TEXT_MARKS combining marks in a row, since a taller
 * stack draws over the lines around it. When the cap stops it early it ends
 * with U+2026, so up to `max` + 3 bytes in all. The source bytes are left
 * alone. */
#define TEXT_MARKS 2u
void text_clean(text_line *l, const sc_arena *sa, sc_str s, size_t max);
void text_u64(text_line *l, uint64_t n, bool sep);
void text_ms(text_line *l, uint64_t ms); /* "850 ms", "1.2 s", as sc_buf_ms */
/* The built string, or the canonical empty string after a failure. */
sc_str text_view(const text_line *l);

/* `s` (live in `sa`) less a last character cut short by the end: what a
 * value truncated at a byte count may end with. Other bytes are kept. */
sc_str text_whole_chars(const sc_arena *sa, sc_str s);

/* A grid cell's display copy (plan U11): as text_clean, except a newline
 * shows as U+21B5 and a tab as U+2192 on the one line, and a truncated value
 * ends with U+2026 (at most `max` + 6 bytes). */
sc_str text_cell_copy(sc_arena *a, const sc_arena *sa, sc_str s, size_t max, bool truncated,
                      bool *lost);

/* A display copy of `s`, as text_clean makes it (at most `max` + 3 bytes). */
sc_str text_clean_copy(sc_arena *a, const sc_arena *sa, sc_str s, size_t max, bool *lost);

/* The first `n` bytes of `s` (live in `sa`), cut back to a character
 * boundary (sc_utf8_truncate), as a display copy, then an ellipsis: the cut a
 * view makes when text is too wide. */
sc_str text_cut(sc_arena *a, const sc_arena *sa, sc_str s, size_t n, bool *lost);

/* A terminal reply's items. The strings are views into its arena. */
typedef struct {
    bool has_tag;
    sc_str tag; /* the last statement's */
    bool error;
    sc_str sqlstate, message, detail, hint;
    uint64_t position; /* 1-based character, 0 for none */
} query_terminal;

/* Reads the terminal reply in `a` (NULL reads as nothing). Items after a
 * malformed one are not read. */
query_terminal query_terminal_read(const sc_arena *a);

/* The toolbar's connection dot and label. A broken link reads as
 * disconnected. */
void query_connection(sc_arena *a, ui_conn_state state, bool broken, uint8_t *dot, sc_str *label);

/* The bottom bar's centre text and its tick or cross (SHELL_STATUS_*) for the
 * query in `u`, whose terminal reply `t` holds (live in `ta`). */
sc_str query_count(sc_arena *a, const conn_ui *u, const sc_arena *ta, const query_terminal *t,
                   uint64_t elapsed_ms, uint8_t *status, bool *lost);

/* The query toolbar's stop button (plan U13): Cancel, greyed while there is
 * nothing to cancel, and Disconnect in its place once a cancel has run 5 s
 * or failed. `u` is the selected tab's query; `live` says the tab owns the
 * link's run, and `waiting` that it queues for the link. */
typedef enum { QUERY_STOP_DISABLED, QUERY_STOP_CANCEL, QUERY_STOP_DISCONNECT } query_stop;
query_stop query_stop_for(const conn_ui *u, bool live, bool waiting);

/* Whether the view offers Reconnect: the connection was lost, failed or
 * broke, or the user disconnected (`state` then reads DISCONNECTED). */
bool query_reconnect_offered(ui_conn_state state, bool broken);

/* The Reconnect button's text: "Reconnect", or, while both retiring slots
 * are busy, "Waiting for the old connection" (KTD19). */
sc_str query_reconnect_label(sc_arena *a, bool enabled);

/* A note under the editor about the last key, command or sidebar open. */
enum {
    QUERY_NOTICE_NONE = 0,
    QUERY_NOTICE_TOO_BIG,       /* a key or paste would overflow the editor */
    QUERY_NOTICE_NOT_TEXT,      /* a key or paste was not UTF-8, or held a control character */
    QUERY_NOTICE_STALE,         /* a key or paste saw a stale slice or failed gap move */
    QUERY_NOTICE_CLIP_TOO_BIG,  /* the clipboard holds more than the editor */
    QUERY_NOTICE_BUSY,          /* run refused: a query is active */
    QUERY_NOTICE_NOT_CONNECTED, /* run refused: no connection */
    QUERY_NOTICE_POOL,          /* run refused: no result arena free */
    QUERY_NOTICE_TABS_FULL,
    QUERY_NOTICE_RUN_TOO_BIG
};

/* The note's text; empty for QUERY_NOTICE_NONE. */
sc_str query_notice(sc_arena *a, uint8_t notice);
/* editor_status from editor_insert → QUERY_NOTICE_*. */
uint8_t query_insert_notice(uint8_t st);

#endif
