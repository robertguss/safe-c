#ifndef EXPLORER_SQLKW_H
#define EXPLORER_SQLKW_H

#include "safec/str.h"
#include "safec/tslice.h"
#include "sqltok.h"

typedef struct {
    const sc_arena *arena;
    sc_tslice words;
    uint8_t dialect;
} sqlkw;

/* Builds the keyword set in `a`. False when `a` is too small. */
bool sqlkw_init(sqlkw *k, sc_arena *a, uint8_t dialect);
/* Whether `word`, in arena `wa`, is a keyword, ignoring ASCII case. An empty
 * or stale word is not. */
bool sqlkw_is(const sqlkw *k, const sc_arena *wa, sc_str word);

#endif
