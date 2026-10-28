#ifndef EXPLORER_PREFS_H
#define EXPLORER_PREFS_H

#include "conncfg.h"

typedef struct { uint32_t lines_per_row, rows_per_page; size_t line; } prefs;
prefs prefs_parse(const sc_arena *a, sc_str file);

#endif
