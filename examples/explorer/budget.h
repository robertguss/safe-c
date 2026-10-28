#ifndef EXPLORER_BUDGET_H
#define EXPLORER_BUDGET_H

/* The result pool's budgets (plan KTD19). One pool is shared by every
 * connection and split into classes; each arena taken is charged to one class
 * and one owner until it comes back. Core code: it includes only the stdlib.
 *
 * - Control: each link's two KTD5 reserved arenas, for every link slot.
 * - Metadata: the metadata link's own arenas, for its runs and grants.
 * - Floor: only a query run's first arenas: its RUN (which carries the
 *   terminal reply), and the COLUMNS and first chunk of a set, one at a time.
 *   A run that has to take its first grant from the floor so gets one chunk
 *   arena instead of the window.
 * - Growth: everything else the query link takes, still capped per result by
 *   its quota (KTD11).
 *
 * A query RUN takes the floor first; grants take growth first. A kept result
 * holds at most 1 + BUDGET_FLOOR_GRANTS floor arenas, and the floor has room
 * for that many for every tab and one live run, so while results hold every
 * growth arena a run in any tab still gets its RUN, COLUMNS and first chunk,
 * and returns its first rows. The pool book (conn.h) picks which free arena;
 * this only says which class may pay for it. */

#include "safec/tslice.h"

#define BUDGET_TABS 9u         /* TABS_MAX, which tabs.c checks */
#define BUDGET_FLOOR_GRANTS 2u /* a set's COLUMNS and its first chunk */

/* The starting split: 192 arenas (48 MiB). */
#define BUDGET_CONTROL_ARENAS 8u /* two for each of four link slots */
#define BUDGET_META_ARENAS 16u
#define BUDGET_FLOOR_ARENAS ((BUDGET_TABS + 1u) * (1u + BUDGET_FLOOR_GRANTS))
#define BUDGET_GROWTH_ARENAS \
    (192u - BUDGET_CONTROL_ARENAS - BUDGET_META_ARENAS - BUDGET_FLOOR_ARENAS)

/* Link slots that may be retiring at once (KTD19): a link the user
 * disconnected or rebuilt, polled until its worker says BYE. */
#define BUDGET_RETIRING_SLOTS 2u

/* Who an arena is charged to. A retiring link's arenas move to
 * BUDGET_RETIRING, which never takes any: they come back as its worker
 * returns them. */
typedef enum { BUDGET_QUERY, BUDGET_META, BUDGET_RETIRING } budget_owner;

typedef enum {
    BUDGET_FROM_NONE,
    BUDGET_FROM_CONTROL,
    BUDGET_FROM_META,
    BUDGET_FROM_FLOOR,
    BUDGET_FROM_GROWTH
} budget_class;

typedef enum {
    BUDGET_USE_CONTROL, /* a link's reserved arena */
    BUDGET_USE_RUN,     /* a RUN, which becomes the terminal reply */
    BUDGET_USE_GRANT    /* a chunk credit: COLUMNS or CHUNK */
} budget_use;

typedef struct {
    size_t control, meta, floor, growth;
} budget_split;

typedef struct {
    budget_split cap;
    budget_split used;
    size_t query_held; /* every arena charged to an owner, control included */
    size_t meta_held;
    size_t retiring_held;
    size_t count;      /* the pool size: the split's sum */
    sc_arena *arena;   /* holds `slots` */
    sc_tslice slots;   /* budget_slot per arena: who pays for it */
} budget;

/* The starting split. */
budget_split budget_default_split(void);

/* A budget for a pool of the split's sum, every arena free; `a` holds one
 * record per arena. False on an empty split or no room. */
bool budget_init(budget *b, sc_arena *a, budget_split cap);

/* The class that pays for `use` by `owner`, or BUDGET_FROM_NONE. For a grant,
 * `set` counts the set's arenas held and out, and `out` the grants out. */
budget_class budget_pick(const budget *b, budget_owner owner, budget_use use, size_t set,
                         size_t out);

/* Charges free arena `i` to `owner` and class `c`. False when `i` is past the
 * pool, already charged, or `c` is full. */
bool budget_take(budget *b, size_t i, budget_owner owner, budget_class c);

/* Charged arena `i` passes to BUDGET_RETIRING (plan KTD19, U13). A floor or
 * metadata arena moves to growth while growth has a spare one, so a retiring
 * link leaves the floor to the runs after it, and the metadata class to the
 * metadata link that replaces it (U18). False when `i` is not charged. */
bool budget_retire(budget *b, size_t i);

/* Arena `i` is free again. False when it was not charged. */
bool budget_put(budget *b, size_t i);

/* The class arena `i` is charged to, or BUDGET_FROM_NONE. */
budget_class budget_class_of(const budget *b, size_t i);

/* Free arenas `c` still has. */
size_t budget_spare(const budget *b, budget_class c);

/* Arenas `owner` could take for a run now: floor and growth for the query
 * link, the metadata class for the metadata link, none for a retiring one. */
size_t budget_room(const budget *b, budget_owner owner);

/* The most `owner` can hold outside control: the size of its classes (0 for
 * BUDGET_RETIRING). */
size_t budget_limit(const budget *b, budget_owner owner);

/* Arenas charged to `owner`, control included. */
size_t budget_held(const budget *b, budget_owner owner);

#endif
