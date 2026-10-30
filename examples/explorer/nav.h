#ifndef EXPLORER_NAV_H
#define EXPLORER_NAV_H

/* A tab's view stack (plan U20, KTD23): up to NAV_MAX entries, each a query
 * view or a table view (its oid, Content or Structure, its page and its
 * pager's total). Opening an object pushes an entry after the current one
 * and drops the forward entries; past NAV_MAX the oldest goes. ‹ › and
 * ⌥⌘←/→ move through it. Core code: no SDL.
 *
 * A tab starts with one query view, and only an object opens, so the query
 * view is the stack's first entry or gone. Its SQL text is the tab's editor,
 * which the app leaves alone while a table view is shown: going back to it
 * restores the text, with no result and no run (nav_arrival). */

#include "frame.h"
#include "page.h"

#define NAV_MAX 16u
#define NAV_NAME_BYTES 4096u
#define NAV_NAMES_BYTES (NAV_MAX * NAV_NAME_BYTES * 2u)

typedef enum { NAV_QUERY, NAV_TABLE } nav_kind;
typedef enum { NAV_CONTENT, NAV_STRUCTURE } nav_view;

typedef struct {
    nav_kind kind;
    uint32_t schema, oid; /* a table view's relation */
    nav_view view;
    uint64_t page;        /* from 1 */
    page_total total;
    uint64_t gen;
    sc_str schema_name, name;
    catalog_rel_kind relation_kind;
    size_t name_slot;
    bool gone;
} nav_entry;

typedef struct {
    sc_arena *arena;
    sc_tslice entries; /* nav_entry, NAV_MAX */
    size_t count;
    size_t at;
    sc_slice names;
} nav_stack;

/* A stack in `a` holding one query view. */
bool nav_init(nav_stack *n, sc_arena *a);
/* Back to one query view, as a new tab starts. */
void nav_reset(nav_stack *n);

/* A table view of relation `oid`, Content, page 1, with `total`. */
nav_entry nav_table(uint32_t schema, uint32_t oid, page_total total);

/* Pushes `e` after the current entry: the forward entries are dropped, and
 * the oldest when NAV_MAX are held. `e` is current. */
bool nav_push(nav_stack *n, nav_entry e);
bool nav_open(nav_stack *n, const catalog *c, uint32_t schema, uint32_t oid, page_total total);
void nav_resolve(nav_stack *n, catalog *c);
bool nav_can_back(const nav_stack *n);
bool nav_can_forward(const nav_stack *n);
/* Moves one entry; false at the end. */
bool nav_back(nav_stack *n);
bool nav_forward(nav_stack *n);
bool nav_current(const nav_stack *n, nav_entry *out);
/* Replaces the current entry: a page, a Content/Structure switch or a total. */
bool nav_set_current(nav_stack *n, nav_entry e);

/* A move through the stack: ‹ ›, the Content/Structure switch, and the
 * pager's ‹ ›. */
typedef enum { NAV_MOVE_BACK, NAV_MOVE_FORWARD, NAV_MOVE_SWITCH, NAV_MOVE_PREV_PAGE, NAV_MOVE_NEXT_PAGE } nav_move;

/* The tab's run: `busy` while it is live or waiting; `ended` once it has
 * ended with `rows` rows, `capped` when memory cut it short. */
typedef struct {
    bool busy, ended;
    uint64_t rows;
    bool capped;
} nav_run;

/* Whether move `m` is allowed now. Back and forward need an entry that
 * way, and never leave a query view while its run is busy: the user's
 * query is not cancelled by a move (a running page is abandoned). The
 * switch needs a table view; the pages a Content view that is not busy,
 * page_can_prev for ‹ and page_can_next for ›. */
bool nav_can_move(const nav_stack *n, nav_move m, nav_run r);
/* Makes move `m` when nav_can_move allows it; false, and no change, when
 * not. */
bool nav_apply(nav_stack *n, nav_move m, nav_run r);

/* What the app does on arriving at entry `e` (after a move, a push, a
 * switch or a page). It always frees the result of the view it left first.
 * QUERY: shows the editor's SQL, with no result and no run. CONTENT: runs
 * the page. STRUCTURE: shows the relation's detail, with no run. */
typedef enum { NAV_SHOW_QUERY, NAV_SHOW_CONTENT, NAV_SHOW_STRUCTURE } nav_arrival;
nav_arrival nav_arrive(nav_entry e);

/* The navigation keys: ⌥⌘← back, ⌥⌘→ forward, ⌥⌘T switches Content and
 * Structure (⌘ is UI_MOD_CMD: Ctrl on Linux). */
typedef enum { NAV_KEY_NONE, NAV_KEY_BACK, NAV_KEY_FORWARD, NAV_KEY_SWITCH } nav_key;
nav_key nav_key_for(uint32_t key, uint16_t mods, uint8_t platform);

#endif
