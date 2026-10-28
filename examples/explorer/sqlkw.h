#ifndef EXPLORER_SQLKW_H
#define EXPLORER_SQLKW_H

#include "safec/str.h"
#include "safec/tslice.h"

typedef struct {
    const sc_arena *arena;
    sc_tslice words;
} sqlkw;

/* Builds the keyword set in `a`. False when `a` is too small. */
bool sqlkw_init(sqlkw *k, sc_arena *a);
/* Whether `word`, in arena `wa`, is a keyword, ignoring ASCII case. An empty
 * or stale word is not. */
bool sqlkw_is(const sqlkw *k, const sc_arena *wa, sc_str word);

#endif
