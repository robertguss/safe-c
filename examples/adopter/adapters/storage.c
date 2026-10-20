#include "storage.h"

static _Alignas(8) uint8_t backing[512];

void adopter_arena_init(sc_arena *a) {
    sc_arena_init(a, backing, sizeof backing);
}
