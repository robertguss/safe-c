#ifndef EXPLORER_EDITOR_H
#define EXPLORER_EDITOR_H

/* The SQL editor (plan U22, KTD25): a gap buffer with a cursor and an anchor,
 * a bounded undo ring, and highlighting through checkpoints of the resumable
 * lexer (sqltok.h). editor.c holds the buffer, undo and movement;
 * editor_syntax.c the checkpoints, the statement at the caret and the
 * highlighting. Core code: it includes only the stdlib, sqltok, sqlkw and
 * frame.h (for the command-key mapping), so tests/explorer_editor_test.c
 * links it with those alone.
 *
 * The buffer holds valid UTF-8 with no control character but tab and
 * newline; an insert drops carriage returns and refuses anything else, or
 * text that would not fit, whole. Offsets are bytes; movement is by code
 * point. Every edit replaces the selection. `ms` is the caller's clock, for
 * grouping a typing run into one undo step. */

#include "sqlkw.h"
#include "sqltok.h"
#include "safec/buf.h"
#include "safec/tslice.h"
#include "safec/utf8.h"

#define EDITOR_CAP (1u << 20)
#define EDITOR_UNDO_STEPS 100u
#define EDITOR_UNDO_BYTES (EDITOR_CAP * 2u + EDITOR_CAP / 4u)
#define EDITOR_GROUP_MS 500u
#ifndef EDITOR_WINDOW
#define EDITOR_WINDOW 4096u
#endif
#define EDITOR_CHECKPOINTS 1024u
#define EDITOR_TAG_MAX 64u
#define EDITOR_COMPOSE_CAP 256u
#define EDITOR_BYTES (EDITOR_CAP + EDITOR_UNDO_BYTES + EDITOR_UNDO_STEPS * 96u + \
                      EDITOR_CHECKPOINTS * (64u + EDITOR_TAG_MAX) + EDITOR_TAG_MAX + EDITOR_COMPOSE_CAP + 512u)
/* The shared scratch every editor operation resets: a flat copy of a buffer. */
#define EDITOR_SCRATCH_BYTES (EDITOR_CAP + 4u * EDITOR_WINDOW)

typedef enum { EDITOR_OK, EDITOR_TOO_BIG, EDITOR_NOT_TEXT, EDITOR_STALE } editor_status;

typedef struct {
    size_t at, removed, inserted, bytes;
    size_t cursor, anchor, after;
    uint64_t ms;
    uint8_t kind;
} editor_step;

typedef struct {
    size_t off, seen, depth;
    uint8_t mode, quote, tag_len;
} editor_checkpoint;

typedef struct {
    sc_arena *arena;
    sc_arena *scratch;
    sc_slice text;
    size_t gap, gap_end;
    size_t cursor, anchor;
    sc_slice store;
    size_t store_used;
    sc_tslice steps;
    size_t first, count, done;
    bool sealed;
    sc_tslice cps;
    sc_slice tags;
    size_t cp_count;
    sc_slice tag_live;
    bool std_strings;
    size_t lines;
    size_t top, top_off, left;
    bool follow;
    sc_slice compose;
    size_t compose_len, compose_caret;
    uint64_t version;
    size_t relexed;
    uint64_t stmt_version;
    size_t stmt_cursor, stmt_from, stmt_to;
    bool stmt_known, stmt_some;
} editor;

/* An empty editor in `a` (EDITOR_BYTES), using the shared `scratch`
 * (EDITOR_SCRATCH_BYTES). `std` is standard_conforming_strings. */
bool editor_init(editor *e, sc_arena *a, sc_arena *scratch, bool std);
/* Empties the buffer, its history and its composition. */
bool editor_clear(editor *e);
/* Relexes the whole buffer when standard_conforming_strings changes. */
void editor_set_std(editor *e, bool std);

size_t editor_len(const editor *e);
/* The byte at `i`; false past the end. */
bool editor_byte(const editor *e, size_t i, uint8_t *out);
/* Bytes [from, to) copied into `dst`; empty when they do not fit. */
sc_str editor_copy(const editor *e, size_t from, size_t to, sc_arena *dst);

/* The selection as [from, to); empty when the anchor is at the cursor. */
void editor_selection(const editor *e, size_t *from, size_t *to);
/* Puts the anchor and the cursor at code-point boundaries at or before the
 * offsets given; ends the undo step. */
void editor_select(editor *e, size_t anchor, size_t cursor);
/* Selects the word, or the run of other characters, holding `off`. */
void editor_select_word(editor *e, size_t off);
/* Selects the line holding `off`, with its newline. */
void editor_select_line(editor *e, size_t off);

/* Replaces the selection with `s` (live in `sa`). A typed run joins the
 * last undo step while it stays contiguous and comes within
 * EDITOR_GROUP_MS; a paste is a step of its own. Text arriving while a
 * composition is open commits it: the composition closes and the step ends. */
editor_status editor_insert(editor *e, const sc_arena *sa, sc_str s, bool typed, uint64_t ms);

typedef enum {
    EDITOR_CMD_NONE,
    EDITOR_CMD_NEWLINE,
    EDITOR_CMD_TAB,
    EDITOR_CMD_BACKSPACE,
    EDITOR_CMD_DELETE,
    EDITOR_CMD_LEFT,
    EDITOR_CMD_RIGHT,
    EDITOR_CMD_UP,
    EDITOR_CMD_DOWN,
    EDITOR_CMD_WORD_LEFT,
    EDITOR_CMD_WORD_RIGHT,
    EDITOR_CMD_LINE_START,
    EDITOR_CMD_LINE_END,
    EDITOR_CMD_DOC_START,
    EDITOR_CMD_DOC_END,
    EDITOR_CMD_SELECT_ALL,
    EDITOR_CMD_UNDO,
    EDITOR_CMD_REDO,
    EDITOR_CMD_COMMENT,
    EDITOR_CMD_INDENT,
    EDITOR_CMD_OUTDENT,
    EDITOR_CMD_COPY,
    EDITOR_CMD_CUT,
    EDITOR_CMD_PASTE,
    EDITOR_CMD_ESCAPE
} editor_cmd;

typedef struct {
    editor_cmd cmd;
    bool extend; /* ⇧ with a movement: extend the selection */
} editor_press;

/* The press for SDL keycode `key` with UI_MOD_* bits `mods` on `platform`.
 * ⌘ is UI_MOD_CMD (Ctrl on Linux); on the Mac, ^A and ^E go to the line's
 * start and end (KTD26). Running and the tab keys are global (focus.h). */
editor_press editor_press_for(uint32_t key, uint16_t mods, uint8_t platform);

/* Carries out a press other than COPY, CUT and PASTE, which need the
 * clipboard (the caller cuts with editor_insert of empty text). True when the
 * buffer, the selection or the composition changed. ESCAPE closes a
 * composition. */
bool editor_do(editor *e, editor_press p, uint64_t ms);

/* The IME's uncommitted text, its caret in bytes, drawn at the cursor. It
 * enters neither the buffer nor the history; empty text closes it. Text
 * longer than EDITOR_COMPOSE_CAP is cut at a code point. */
void editor_compose(editor *e, const sc_arena *sa, sc_str s, size_t caret);
sc_str editor_compose_text(const editor *e);

/* The statement holding the cursor, as [from, to) without the space and
 * comments around it: the one whose text starts at or before the cursor,
 * else the first. False when the buffer holds no statement. */
bool editor_statement(editor *e, size_t *from, size_t *to);
/* What a run sends, as [from, to): the whole buffer when `all`, else the
 * selection, else the current statement, else nothing. Ends the undo step. */
void editor_run_range(editor *e, bool all, size_t *from, size_t *to);

/* Line geometry: the line's first byte, the byte of its newline (or the
 * end), its index from 0, and code points from `from` to `to` on one line. */
size_t editor_line_start(const editor *e, size_t off);
size_t editor_line_end(const editor *e, size_t off);
size_t editor_line_of(const editor *e, size_t off);
size_t editor_columns(const editor *e, size_t from, size_t to);
/* The offset `cols` code points on from `from`, stopping at the line's end. */
size_t editor_advance(const editor *e, size_t from, size_t cols);
/* The start of line `top + k`, or of the last line. */
size_t editor_line_after_top(const editor *e, size_t k);

/* Keeps the cursor inside `rows` lines and `cols` columns after it moved. */
void editor_scroll(editor *e, size_t rows, size_t cols);
/* Scrolls by `lines` (down when positive) without moving the cursor. */
bool editor_wheel(editor *e, int32_t lines);

enum { EDITOR_INK_TEXT, EDITOR_INK_KEYWORD, EDITOR_INK_STRING, EDITOR_INK_COMMENT };

typedef struct {
    size_t from, to;
    uint8_t ink;
} editor_span;

/* The highlighting of [from, to) as runs of one ink, in order, into `dst`
 * (editor_span elements); the count, or 0 when they do not fit. Lexes from the
 * checkpoint at or before `from`. */
size_t editor_spans(editor *e, const sqlkw *kw, size_t from, size_t to, sc_arena *dst, sc_tslice *out);

/* Shared by editor.c and editor_syntax.c: moves `n` bytes of `s` from `from`
 * to `to`, overlapping or not; and re-lexes after an edit at `at` that
 * replaced `removed` bytes with `inserted`. */
bool editor_move(const sc_arena *a, sc_slice s, size_t to, size_t from, size_t n);
void editor_relex(editor *e, size_t at, size_t removed, size_t inserted);

#endif
