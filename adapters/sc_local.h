#ifndef SAFEC_ADAPTER_LOCAL_H
#define SAFEC_ADAPTER_LOCAL_H

#include "sc_db.h"

#define SC_LOCAL_COLUMNS 1664u
#define SC_LOCAL_ERROR 8192u

typedef struct {
    char *name;
    char *type;
    bool numeric;
} sc_local_column;

typedef struct {
    sc_db_engine engine;
    sc_db_control *control;
    char *path;
    char *sql;
    sc_local_column *columns;
    size_t cols;
    size_t rows;
    bool ready;
    bool ended;
    bool row_error;
    uint64_t transaction;
    char error[SC_LOCAL_ERROR];
    char code[32];
    char tag[64];
} sc_local;

typedef struct {
    char bytes[4096];
    size_t len;
    size_t cap;
    bool failed;
    sc_db_control *control;
} sc_local_text;

void sc_local_text_bytes(sc_local_text *out, const char *s, size_t n);
void sc_local_text_cstr(sc_local_text *out, const char *s);
void sc_local_text_hex(sc_local_text *out, const uint8_t *s, size_t n);
char *sc_local_copy(const sc_arena *a, sc_str s);
bool sc_local_path(sc_local *s, const sc_arena *a, sc_str path);
void sc_local_error(sc_local *s, const char *code, const char *message);
void sc_local_clear_columns(sc_local *s);
void sc_local_free(sc_local *s);
bool sc_local_u64(const sc_arena *a, sc_buf *b, uint64_t n);
bool sc_local_string(const sc_arena *a, sc_buf *b, const char *s, size_t n);
bool sc_local_columns(sc_local *s, const sc_arena *a, sc_buf *b);
bool sc_local_put_error(sc_local *s, const sc_arena *a, sc_buf *b);
bool sc_local_cell(const sc_arena *a, sc_buf *b, sc_local_text *text, bool null);

sc_result_size sc_duckdb_open(sc_db *c, const sc_arena *a, sc_str path);
sc_result_size sc_duckdb_send(sc_local *s, const sc_arena *a, sc_str sql);
sc_db_next_kind sc_duckdb_next(sc_local *s);
bool sc_duckdb_row(sc_local *s, size_t row, size_t cap, const sc_arena *a, sc_buf *b);
sc_db_cancel_status sc_duckdb_cancel(sc_local *s);
void sc_duckdb_finish(sc_local *s);

#endif
