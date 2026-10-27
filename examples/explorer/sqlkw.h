#ifndef EXPLORER_SQLKW_H
#define EXPLORER_SQLKW_H

/* The SQL keywords the editor highlights (plan U14): Postgres's reserved
 * keywords and the common statement words. R6 rules out a static table, so
 * init builds a sorted sc_tslice of sc_str in an arena from one literal, and
 * lookup is a binary search, ASCII case-insensitive. Core code: no SDL. */

#include "safec/str.h"
#include "safec/tslice.h"

typedef struct {
    const sc_arena *arena;
    sc_tslice words; /* sc_str, strictly ascending, compared upper-cased */
} sqlkw;

/* Builds the keyword set in `a`. False when `a` is too small. */
bool sqlkw_init(sqlkw *k, sc_arena *a);
/* Builds a set from `list`, which lives in `a`: words split by single
 * spaces, strictly ascending in ASCII upper case. False for an empty word,
 * a word out of order or repeated, or `a` too small. */
bool sqlkw_init_list(sqlkw *k, sc_arena *a, sc_str list);
/* Whether `word`, in arena `wa`, is a keyword, ignoring ASCII case. An empty
 * or stale word is not. */
bool sqlkw_is(const sqlkw *k, const sc_arena *wa, sc_str word);

#endif
