#ifndef EXPLORER_TABS_H
#define EXPLORER_TABS_H

/* Result tabs (plan U12): the strip's tab width, what a click on it hits,
 * which tab is selected after a close, the tab keys, a tab's title, and the
 * order queued runs start in. Core code: integer points, no SDL.
 *
 * The strip runs across the top of the content column (U16). Tabs share it
 * equally, up to TAB_MAX_WIDTH each, from the left; each has a close box at
 * its left end, and the + follows the last tab. The view tags each tab, close
 * box and the +, and clicks are tested against the rectangles the adapter
 * reports for them (KTD18). */

#include "frame.h"
#include "safec/utf8.h"

#define TABS_MAX 9u /* ⌘1 to ⌘9 reach every tab */
#define TAB_STRIP_HEIGHT 28u
#define TAB_MAX_WIDTH 220u
#define TAB_PLUS_WIDTH 28u
#define TAB_CLOSE_SIZE 16u
#define TAB_CLOSE_INSET 6u /* the close box's left edge, from the tab's */

/* Node tags: tab i is TAB_TAG_SELECT + i and its close box TAB_TAG_CLOSE + i. */
#define TAB_TAG_SELECT 0x100u
#define TAB_TAG_CLOSE 0x200u
#define TAB_TAG_NEW 0x300u /* the + */

/* Each of `count` tabs' width in a strip `strip_w` wide, leaving room for the
 * + until TABS_MAX are open; 0 when there are none or no room. */
uint32_t tab_width(uint32_t strip_w, size_t count);

typedef enum { TAB_HIT_NONE, TAB_HIT_SELECT, TAB_HIT_CLOSE, TAB_HIT_NEW } tab_hit_kind;

typedef struct {
    tab_hit_kind kind;
    size_t tab;
} tab_hit;

/* What a press of `button` at (x, y) lands on, from the last frame's
 * rectangles `l`, with `count` tabs open. Only the left button hits. */
tab_hit tab_hit_at(const ui_layout *l, size_t count, int32_t x, int32_t y, uint8_t button);

/* The selected tab's index once tab `closed` of `count` is gone: the same tab
 * when it sits before the closed one, else its right neighbour, else the new
 * last tab. 0 when none are left. */
size_t tab_after_close(size_t count, size_t selected, size_t closed);

typedef enum {
    TAB_KEY_NONE,
    TAB_KEY_NEW,      /* ⌘T */
    TAB_KEY_CLOSE,    /* ⌘W */
    TAB_KEY_SELECT,   /* ⌘1 to ⌘9: `tab` */
    TAB_KEY_LOAD_ALL  /* ⇧⌘↩: run again with only the memory quota */
} tab_key_kind;

typedef struct {
    tab_key_kind kind;
    size_t tab;
} tab_key;

/* For SDL keycode `key` with UI_MOD_* bits `mods` on `platform`
 * (UI_PLATFORM_*). ⌘ is UI_MOD_CMD; Alt never makes a tab key, nor does Ctrl
 * on the Mac, where it is a modifier of its own (KTD26). */
tab_key tab_key_for(uint32_t key, uint16_t mods, uint8_t platform);

/* A tab's title from its SQL (live in `sa`): the first line with any
 * non-space text, without its leading and trailing spaces, cut to at most
 * `max` bytes at a character boundary. `cut` says text followed: more of the
 * line, or more lines. Empty when the SQL is blank. */
sc_str tab_title(const sc_arena *sa, sc_str sql, size_t max, bool *cut);

typedef enum { TAB_RUN_OK, TAB_RUN_NOT_CONNECTED, TAB_RUN_POOL } tab_run_check;

/* Whether a tab's run can go out, decided before its old result is freed so a
 * refused run keeps it: the connection must be `usable` (connected or
 * connecting, not broken), and the `free` arenas its budget can pay for plus
 * the `reclaim` its result would free must cover the RUN arena, COLUMNS and a
 * first chunk. */
tab_run_check tab_can_run(bool usable, size_t free, size_t reclaim);

/* The tab whose run is next: the smallest non-zero entry of `waits`
 * (uint64_t queue numbers, 0 for not waiting) among the first `count`.
 * SIZE_MAX when none waits. */
size_t tab_next_waiting(const sc_arena *a, sc_tslice waits, size_t count);

/* ---- The tab book: which tabs are open, selected, running and waiting ----
 *
 * The app keeps each tab's contents; the book keeps the bookkeeping, so the
 * rules for queueing, starting and closing are tested here. One run at a
 * time owns the connection (`live`); a run asked for while the connection is
 * active (a query running, or one cancelling after its tab closed) waits, and
 * waiting tabs start one at a time, in the order they asked, once it is idle. */

#define TAB_NONE SIZE_MAX

typedef struct {
    size_t count;   /* open tabs: 0..count-1 */
    size_t sel;     /* the selected tab */
    size_t live;    /* the tab whose run owns the connection, or TAB_NONE */
    uint64_t seq;   /* the last queue number handed out */
    sc_arena *arena; /* holds `waits` */
    sc_tslice waits; /* uint64_t, TABS_MAX: each tab's queue number, 0 when not waiting */
} tab_book;

/* One open, selected tab; `a` holds the waits. */
bool tab_book_init(tab_book *b, sc_arena *a);

typedef enum { TAB_RUN_START, TAB_RUN_QUEUED, TAB_RUN_BUSY } tab_run_kind;

/* Tab `i` asks to run while the connection is `active` or idle: BUSY when it
 * is the live tab and still running; QUEUED (keeping an earlier place) when
 * another run owns the connection or the connection is active; else START,
 * and the caller then calls tab_book_started or tab_book_refused. */
tab_run_kind tab_book_run(tab_book *b, size_t i, bool active);
/* Tab `i`'s run went out: it is live and no longer waits. */
void tab_book_started(tab_book *b, size_t i);
/* Tab `i`'s run was refused: it no longer waits. */
void tab_book_refused(tab_book *b, size_t i);
/* Whether tab `i` waits for the connection. */
bool tab_book_waiting(const tab_book *b, size_t i);
/* Once the connection is idle, the live tab's run has ended: returns that tab
 * (its result is the caller's to keep) and clears `live`. TAB_NONE otherwise. */
size_t tab_book_finished(tab_book *b, bool active);
/* The tab to start now: the longest-waiting one, when no tab is live and the
 * connection is idle. TAB_NONE otherwise. */
size_t tab_book_next(const tab_book *b, bool active);
typedef enum {
    TAB_CANCEL_NONE,     /* tab `i` has no run to cancel */
    TAB_CANCEL_DEQUEUED, /* it waited; it no longer does, and the live run carries on */
    TAB_CANCEL_QUERY,    /* it is live: the caller cancels its query */
    TAB_CANCEL_RETIRE    /* it is live and the link still connects: the caller retires the link */
} tab_cancel_kind;

/* The user cancels tab `i`'s run (plan U13) while the link is `connecting`
 * or not. A waiting tab leaves the queue here; the live tab's run is the
 * caller's to cancel. */
tab_cancel_kind tab_book_cancel(tab_book *b, size_t i, bool connecting);
/* Opens a tab after the last and selects it; false when TABS_MAX are open. */
bool tab_book_open(tab_book *b);
/* Closes tab `i`: later tabs and their places move left, the selection moves
 * as tab_after_close says, and `live` follows its tab. True when `i` was the
 * live tab: the caller abandons its run. The last tab closed leaves none;
 * the caller opens a fresh one. */
bool tab_book_close(tab_book *b, size_t i);

#endif
