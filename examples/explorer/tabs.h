#ifndef EXPLORER_TABS_H
#define EXPLORER_TABS_H

/* Result tabs (plan U12): the strip's geometry and hit testing, which tab is
 * selected after a close, the tab keys, a tab's title, and the order queued
 * runs start in. Core code: integer points, no SDL.
 *
 * The strip runs the window's width under the toolbar. Tabs share it equally,
 * up to TAB_MAX_WIDTH each, from the left; each has a close box at its left
 * end, and the + follows the last tab. The view draws from the same spans the
 * clicks are tested against. */

#include "safec/tslice.h"
#include "safec/utf8.h"

#define TABS_MAX 9u /* ⌘1 to ⌘9 reach every tab */
#define TAB_STRIP_HEIGHT 28u
#define TAB_MAX_WIDTH 220u
#define TAB_PLUS_WIDTH 28u
#define TAB_CLOSE_SIZE 16u
#define TAB_CLOSE_INSET 6u /* the close box's left edge, from the tab's */

typedef struct {
    uint32_t x, w;
} tab_span;

/* Tab `i` of `count` in a strip `strip_w` wide; zero width past the end. */
tab_span tab_span_at(uint32_t strip_w, size_t count, size_t i);
/* The close box inside tab span `t`. */
tab_span tab_close_span(tab_span t);
/* The + after the last of `count` tabs; zero width once TABS_MAX are open. */
tab_span tab_plus_span(uint32_t strip_w, size_t count);

typedef enum { TAB_HIT_NONE, TAB_HIT_SELECT, TAB_HIT_CLOSE, TAB_HIT_NEW } tab_hit_kind;

typedef struct {
    tab_hit_kind kind;
    size_t tab;
} tab_hit;

/* What a click at `x` in the strip lands on. */
tab_hit tab_hit_at(uint32_t strip_w, size_t count, int64_t x);

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

/* ⌘ is GUI or Ctrl, as for the input box; Alt never makes a tab key. */
tab_key tab_key_for(uint32_t key, uint16_t mods);

/* A tab's title from its SQL (live in `sa`): the first line with any
 * non-space text, without its leading and trailing spaces, cut to at most
 * `max` bytes at a character boundary. `cut` says text followed: more of the
 * line, or more lines. Empty when the SQL is blank. */
sc_str tab_title(const sc_arena *sa, sc_str sql, size_t max, bool *cut);

/* The tab whose run is next: the smallest non-zero entry of `waits`
 * (uint64_t queue numbers, 0 for not waiting) among the first `count`.
 * SIZE_MAX when none waits. */
size_t tab_next_waiting(const sc_arena *a, sc_tslice waits, size_t count);

#endif
