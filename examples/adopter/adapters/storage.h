#ifndef ADOPTER_STORAGE_H
#define ADOPTER_STORAGE_H

#include <safec/arena.h>

/* Initializes *a over static backing storage. Application C cannot declare
 * arrays (rule R6), so storage comes from an adapter (rule R0). */
void adopter_arena_init(sc_arena *a);

#endif
