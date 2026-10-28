#ifndef EXPLORER_CONNCFG_H
#define EXPLORER_CONNCFG_H

#include "safec/buf.h"
#include "safec/tslice.h"
#include "safec/utf8.h"

#define CONNCFG_MAX 64u
#define CONNCFG_BYTES (64u << 10)
#define CONNCFG_KEYS 10u

typedef enum { CFG_OK, CFG_PASSWORD, CFG_KEY, CFG_DUPLICATE, CFG_SECTION, CFG_UTF8,
               CFG_LIMIT, CFG_SYNTAX } conncfg_error;
typedef struct { sc_str key, text; } conncfg_pair;
typedef struct {
    sc_str name;
    sc_tslice pairs;
    size_t count, line;
    conncfg_error error;
} conncfg_entry;
typedef struct {
    const sc_arena *source;
    sc_arena *arena;
    sc_tslice entries;
    size_t count, line;
    conncfg_error error;
} conncfg;

bool cfg_line(const sc_arena *a, sc_str file, size_t *at, sc_str *line);
sc_str cfg_trim(const sc_arena *a, sc_str s);
bool cfg_pair(const sc_arena *a, sc_str line, conncfg_pair *out);
bool conncfg_parse(conncfg *cfg, const sc_arena *a, sc_str file, sc_arena *out);
bool conncfg_at(const conncfg *cfg, size_t i, conncfg_entry *out);
bool conncfg_info(const conncfg *cfg, size_t i, sc_arena *out, sc_str *info);
sc_str conncfg_message(sc_arena *a, conncfg_error error);

#endif
