#ifndef EXPLORER_RESULT_H
#define EXPLORER_RESULT_H

/* The UI's model of one streamed result set (plan U10): its COLUMNS message
 * and an index of its CHUNK arenas, so "what is in cell (row, column)" costs a
 * binary search over the chunks and a short skip inside one, for any row.
 *
 * The result only reads the arenas; conn_ui (conn.h) owns and frees them, so a
 * result is cleared whenever its set is replaced and keeps no view past that.
 * Each chunk is checked whole when it is added, so a later read of a cell in
 * range only fails on a broken invariant.
 *
 * Core code: it includes only the stdlib and proto.h. */

#include "proto.h"

#define RESULT_MAX_COLUMNS 1664u /* Postgres's own limit on a row's columns */
#define RESULT_MARKS 64u         /* row marks kept per chunk */

typedef struct {
    const sc_arena *arena; /* the chunk's pool arena */
    size_t index;          /* its pool index */
    uint64_t first;        /* its first row in the set */
    uint64_t rows;
    uint64_t stride;       /* rows between its marks */
} result_chunk;

typedef struct {
    sc_arena *book;     /* holds the index */
    sc_tslice chunks;   /* result_chunk */
    size_t count;
    size_t cap;         /* chunks a set may hold: the quota */
    sc_tslice marks;    /* size_t body offsets, RESULT_MARKS per chunk */
    sc_tslice names;    /* sc_str, live in the columns arena */
    sc_tslice oids;     /* uint32_t */
    bool has_columns;
    const sc_arena *col_arena;
    size_t col_index;
    uint64_t cols;
    uint64_t rows;
} result;

typedef enum {
    RESULT_ADDED,
    RESULT_FULL, /* the index is at capacity: the quota is reached */
    RESULT_BAD   /* not a well-formed message of the kind, or out of order */
} result_add;

/* The book bytes a result with `cap` chunks needs. */
size_t result_book_bytes(size_t cap);

/* An empty result for up to `cap` chunks, its index in `book`. */
bool result_init(result *r, sc_arena *book, size_t cap);

/* Takes the set's COLUMNS message, arena `index` (`a`). A second COLUMNS
 * without a close first is RESULT_BAD. */
result_add result_set_columns(result *r, const sc_arena *a, size_t index);

/* Adds the CHUNK message in arena `index` (`a`) after the rows already held:
 * the mailbox is FIFO and the worker sends a set's chunks in order, so a
 * chunk's first row is the count so far. It must come after COLUMNS and hold
 * exactly its header's rows (at least one) of one cell per column. */
result_add result_add_chunk(result *r, const sc_arena *a, size_t index);

/* Forgets the set. Writes each arena index it held, COLUMNS first, into `out`
 * (size_t) when it has room, and returns how many it held. For a caller that
 * owns the arenas; the explorer's link passes an empty slice, since conn_ui
 * frees them. */
size_t result_close(result *r, sc_arena *oa, sc_tslice out);

/* The chunk holding `row` and the row's offset in it. False past the end. */
bool result_find(const result *r, uint64_t row, size_t *chunk, uint64_t *offset);

/* Column `c`'s name (live in the columns arena) and type oid. */
bool result_column(const result *r, uint64_t c, sc_str *name, uint32_t *oid);

/* Whether values of type `oid` are numbers, shown right-aligned. */
bool result_numeric(uint32_t oid);

/* Reads rows in order from any row. After result_seek or result_next_row the
 * cursor stands at the start of a row; result_next_cell reads its cells in
 * column order. Cell bytes live in `rd.arena`. */
typedef struct {
    const result *r;
    size_t chunk;
    uint64_t left;  /* rows after the current one in this chunk */
    uint64_t cells; /* cells of the current row not yet read */
    proto_reader rd;
} result_cursor;

bool result_seek(result_cursor *c, const result *r, uint64_t row);
bool result_next_row(result_cursor *c);
bool result_next_cell(result_cursor *c, proto_cell *out);

/* Cell (row, col), by seek and skip: for one-off reads. */
bool result_cell(const result *r, uint64_t row, uint64_t col, proto_cell *out);

typedef enum { RESULT_TEXT_HEADER, RESULT_TEXT_CELL, RESULT_TEXT_NULL } result_text;

/* A text's width in points; the glue makes it a display copy and measures it
 * in the font that kind of text is drawn in. `s` is empty for a NULL. */
typedef uint32_t (*result_measure_fn)(void *ctx, const sc_arena *a, sc_str s, result_text kind);

typedef struct {
    result_measure_fn measure;
    void *ctx;
    uint64_t rows;  /* rows of the first chunk read, at most */
    uint32_t pad;   /* added to each width */
    uint32_t max_w; /* the widest a column gets, pad included */
} result_width_rule;

/* The automatic widths of the first `n` columns into `widths` (uint32_t, in
 * `wa`): the widest of the header and the first `rule.rows` cells of the
 * first chunk, plus `pad`, at most `max_w`. Nothing past the first chunk is
 * read. */
bool result_widths(const result *r, sc_arena *wa, sc_tslice widths, size_t n,
                   result_width_rule rule);

#endif
