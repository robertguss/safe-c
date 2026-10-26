#ifndef EXPLORER_PROTO_H
#define EXPLORER_PROTO_H

/* The thread protocol (plan KTD5). Every message between the UI thread and a
 * worker is the index of a pool arena, and ownership of that arena moves with
 * it. The sender writes a fixed header at offset 0 of the arena, then a body of
 * fields. Core code: it includes only the stdlib, so tests link it alone.
 *
 * The header is PROTO_HEADER_BYTES of little-endian u64 words: magic, kind,
 * connection id, request id, two numeric arguments, and the body length. The
 * body is a run of fields, each a u64 or a length-prefixed byte string. */

#include "safec/buf.h"
#include "safec/tslice.h"

#define PROTO_MAGIC 0x31504f5250584553u /* "SEXPROP1", little-endian */
#define PROTO_HEADER_BYTES 56u

/* Message kinds. OPEN to RETURN_TO_WORKER go from the UI to a worker, the rest
 * from a worker to the UI. RETURN hands back an arena with no reply in it: the
 * UI's control arena, a granted or opened chunk the worker did not fill, or a
 * RUN's terminal arena when the worker was not ready or the connection was
 * lost. */
typedef enum {
    PROTO_NONE,
    PROTO_OPEN,             /* UI: the worker's reserved arena; body: conninfo */
    PROTO_RUN,              /* UI: the terminal arena; body: SQL; arg: row limit; aux: quota */
    PROTO_GRANT,            /* UI: one chunk arena for the request */
    PROTO_CANCEL,           /* UI: the UI's control arena; aux: PROTO_CAP_* reason */
    PROTO_CLOSE,            /* UI: the UI's control arena */
    PROTO_RETURN_TO_WORKER, /* UI: the worker's reserved arena, read and handed back */
    PROTO_CONNECTED,        /* worker, in its reserved arena */
    PROTO_CONN_FAILED,      /* worker, in its reserved arena; aux: PROTO_FAIL_*; body: message */
    PROTO_COLUMNS,          /* worker, in a granted arena; body: column names */
    PROTO_CHUNK,            /* worker, in a granted arena; arg: rows in it; body: rows */
    PROTO_DONE,             /* worker, in the terminal arena; arg: rows; aux: earlier tags */
    PROTO_ERROR,            /* worker, in the terminal arena; arg: rows so far */
    PROTO_CAPPED,           /* worker, in the terminal arena; arg: rows; aux: PROTO_CAP_* */
    PROTO_CANCELLED,        /* worker, in the terminal arena; arg: rows kept */
    PROTO_LOST,             /* worker, in its reserved arena */
    PROTO_BYE,              /* worker, in its reserved arena; the worker has exited */
    PROTO_RETURN,           /* worker: an arena handed back unused, under its request id */
    PROTO_KIND_COUNT
} proto_kind;

/* Why a request was cancelled, or why it ended as CAPPED. */
typedef enum {
    PROTO_CAP_USER,   /* the user cancelled */
    PROTO_CAP_LIMIT,  /* the row limit */
    PROTO_CAP_QUOTA,  /* the result's arena quota */
    PROTO_CAP_MEMORY, /* the pool had no arena to grant under quota */
    PROTO_CAP_COUNT
} proto_cap;

/* Why a connection failed. */
typedef enum {
    PROTO_FAIL_NETWORK, /* the server could not be reached, or another failure */
    PROTO_FAIL_AUTH,    /* a password failure (SQLSTATE 28P01 or no password given) */
    PROTO_FAIL_COUNT
} proto_fail;

typedef struct {
    proto_kind kind;
    uint32_t conn;
    uint64_t req;
    uint64_t arg;
    uint64_t aux;
    size_t body_len;
} proto_header;

/* A message being written. The first failure sticks, like frame_builder. */
typedef struct {
    sc_arena *arena;
    sc_slice head;
    sc_buf body;
    proto_header h;
    bool failed;
} proto_writer;

/* A message being read: the body, and the next field's offset in it. */
typedef struct {
    const sc_arena *arena;
    sc_slice body;
    size_t pos;
    bool failed;
} proto_reader;

/* Starts a message in `a`, which must be empty (just reset): the whole arena
 * becomes the message, so nothing else is allocated in it afterwards. Fails
 * for a non-empty arena, an arena too small for the header, or a kind out of
 * range. */
bool proto_begin(proto_writer *w, sc_arena *a, proto_header h);

/* Appends a u64 field, or a byte string `s` live in `sa`. */
bool proto_put_u64(proto_writer *w, uint64_t v);
bool proto_put_str(proto_writer *w, const sc_arena *sa, sc_str s);

/* The body written so far, for code that appends its own bytes (the libpq
 * adapter writes rows into it). Hand it back with proto_set_body. */
sc_buf proto_body(const proto_writer *w);
bool proto_set_body(proto_writer *w, sc_buf b);

/* Writes the header with the body length. Fails if any put failed. */
bool proto_end(proto_writer *w);

/* Reads the header of the message in `a`. Fails, leaving *h alone, when the
 * arena holds fewer bytes than a header, the magic is wrong, the kind or a
 * reason is out of range, or the body length runs past the arena's used
 * bytes. On success *r reads the body. */
bool proto_read(const sc_arena *a, proto_header *h, proto_reader *r);

/* Reads the next field. A string comes back as a view into the message's
 * arena. The first failure sticks, and *out is then left alone. */
bool proto_get_u64(proto_reader *r, uint64_t *out);
bool proto_get_str(proto_reader *r, sc_str *out);

/* Bodies (the libpq adapter writes the COLUMNS, CHUNK and error fields):
 * - OPEN: conninfo (str). RUN: SQL (str). CONN_FAILED: message (str).
 * - COLUMNS: column count (u64), then per column its name (str) and type oid.
 * - CHUNK: `arg` rows, each one cell per column. A cell is a meta word (u64:
 *   the value's true length, | PROTO_CELL_NULL | PROTO_CELL_TRUNCATED), then
 *   the stored bytes (str), at most PROTO_CELL_CAP of them.
 * - DONE, ERROR, CAPPED, CANCELLED: items, each a kind (u64) and its fields:
 *   PROTO_ITEM_TAG and a command tag (str) for each statement that completed;
 *   PROTO_ITEM_ERROR and SQLSTATE, message, detail, hint (str) and the 1-based
 *   character position (u64, 0 for none); then PROTO_ITEM_END and the
 *   transaction status (u64: 0 idle, 1 active, 2 in a transaction, 3 failed
 *   transaction, 4 unknown). */
#define PROTO_CELL_NULL (UINT64_C(1) << 62)
#define PROTO_CELL_TRUNCATED (UINT64_C(1) << 63)
#define PROTO_CELL_CAP 4096u

typedef enum { PROTO_ITEM_END, PROTO_ITEM_TAG, PROTO_ITEM_ERROR } proto_item;

typedef struct {
    sc_str bytes; /* the stored bytes, at most PROTO_CELL_CAP */
    uint64_t len; /* the value's true length */
    bool null;
    bool truncated;
} proto_cell;

/* Reads one cell. Fails, like the other reads, on a short body, and on a cell
 * whose stored bytes disagree with its meta word: more than the cap, a
 * truncated cell storing its whole length or more, a whole cell storing other
 * than its length, or a NULL with a length, the truncated flag, or bytes. */
bool proto_get_cell(proto_reader *r, proto_cell *out);

/* Whether a kind goes from the UI to a worker. */
bool proto_to_worker(proto_kind k);

#endif
