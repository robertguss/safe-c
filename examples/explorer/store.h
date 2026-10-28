#ifndef EXPLORER_STORE_H
#define EXPLORER_STORE_H

#include "sc_posix.h"

sc_str store_path(sc_arena *a, const sc_arena *pa, sc_str dir, const uint8_t *name, size_t n);
#define STORE_PATH(a, pa, dir, name) store_path((a), (pa), (dir), (const uint8_t *)(name), sizeof(name) - 1u)
sc_result_size store_read(const sc_arena *pa, sc_str path, sc_arena *out, size_t cap, sc_str *text);
bool store_save(const sc_arena *pa, sc_str path, sc_str temp, const sc_arena *a, sc_str text);

#endif
