#ifndef EXPLORER_HISTORY_H
#define EXPLORER_HISTORY_H

#include "safec/buf.h"
#include "safec/tslice.h"

#define HISTORY_COUNT 500u
#define HISTORY_BYTES (1u << 20)
#define HISTORY_FILE_BYTES (HISTORY_BYTES + 40000u)

typedef struct { size_t offset, sql_len, name_len; uint64_t time; } history_record;
typedef struct { sc_str sql, name; uint64_t time; } history_entry;
typedef struct {
    sc_arena *arena, *records_arena;
    sc_slice bytes;
    sc_tslice records;
    size_t count, used, dropped;
    bool damaged, dirty;
} history;

bool history_init(history *h, sc_arena *data, sc_arena *records);
bool history_get(const history *h, size_t i, history_entry *out);
bool history_add(history *h, const sc_arena *a, sc_str sql, const sc_arena *na, sc_str name, uint64_t time);
bool history_load(history *h, const sc_arena *a, sc_str file);
bool history_write(const history *h, sc_arena *a, sc_str *out);

#endif
