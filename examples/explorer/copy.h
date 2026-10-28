#ifndef EXPLORER_COPY_H
#define EXPLORER_COPY_H

#include "result.h"

#define COPY_BYTES (4u << 20)

bool copy_field(sc_arena *a, sc_buf *b, const sc_arena *sa, sc_str s, bool null,
                bool numeric, bool csv);
bool copy_rows(sc_arena *a, size_t cap, const result *r, uint64_t first, uint64_t last,
               bool csv, sc_str *out, uint64_t *truncated);

#endif
