#include "adapters/storage.h"
#include <safec/str.h>

/* A minimal adopter: application C inside the subset, built against the
 * installed libsafec through pkg-config. Exit 0 means the library works. */
int main(void) {
    sc_arena arena;
    adopter_arena_init(&arena);
    sc_str greeting = SC_STR_LIT(&arena, "hello from an adopter");
    return SC_STR_EQ_LIT(&arena, greeting, "hello from an adopter") ? 0 : 1;
}
