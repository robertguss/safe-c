#ifndef EXPLORER_INPUT_H
#define EXPLORER_INPUT_H

/* The SQL input box: a fixed-capacity multi-line byte buffer with a
 * cursor. Core code: it includes only the stdlib and frame.h, so
 * tests/explorer_input_test.c links it with frame.c alone.
 *
 * It holds valid UTF-8 with no control character but tab and newline, and
 * drops carriage returns, so a CRLF paste keeps its lines. An insert that is
 * not valid UTF-8, holds another control character (C0, DEL or C1), or would
 * not fit is refused whole, so a paste never leaves half a statement. The
 * cursor and the arrow keys move by character, and backspace removes one.
 *
 * After select-all, the next insert replaces the whole buffer and the next
 * backspace empties it; any cursor move ends the selection. */

#include "../../adapters/sc_ui_frame.h"
#include "safec/buf.h"
#include "safec/utf8.h"

#define INPUT_CAP (64u * 1024u)

typedef enum { INPUT_OK, INPUT_TOO_BIG, INPUT_NOT_TEXT, INPUT_STALE } input_status;

typedef struct {
    sc_arena *arena;
    sc_slice bytes; /* the whole capacity */
    size_t len;
    size_t cursor;  /* 0 .. len */
    bool all;       /* select-all is on */
} input_box;

/* Room for `cap` bytes in `a`. */
bool input_init(input_box *in, sc_arena *a, size_t cap);

/* Inserts `s` (live in `sa`) at the cursor and moves the cursor past it. */
input_status input_insert(input_box *in, const sc_arena *sa, sc_str s);

void input_backspace(input_box *in);
void input_left(input_box *in);
void input_right(input_box *in);
/* Moves to the same column of the line above or below, or the line's end when
 * it is shorter. On the first line, up goes to the start; on the last, down
 * goes to the end. */
void input_up(input_box *in);
void input_down(input_box *in);
/* Moves to the start or the end of the cursor's line. */
void input_line_start(input_box *in);
void input_line_end(input_box *in);
void input_select_all(input_box *in);

/* The text, a view into the box's arena (the canonical empty string when
 * empty). */
sc_str input_text(const input_box *in);

/* The cursor's line and column, both from 0; the column counts characters. */
void input_cursor_at(const input_box *in, size_t *line, size_t *column);

/* The byte offset `cols` characters on from byte offset `from`, stopping at a
 * newline or the end of the text. */
size_t input_advance(const input_box *in, size_t from, size_t cols);

/* What a key press asks of the box when it has focus. ⌘ is UI_MOD_CMD (Ctrl
 * on Linux); on the Mac, Ctrl is its own modifier and ^A and ^E go to the
 * line's start and end (KTD26). Running (⌘↩) and cancelling (Esc) are global
 * keys (focus.h). Text arrives separately, as text events. */
typedef enum {
    INPUT_KEY_NONE,
    INPUT_KEY_NEWLINE,
    INPUT_KEY_TAB,
    INPUT_KEY_BACKSPACE,
    INPUT_KEY_LEFT,
    INPUT_KEY_RIGHT,
    INPUT_KEY_UP,
    INPUT_KEY_DOWN,
    INPUT_KEY_LINE_START,
    INPUT_KEY_LINE_END,
    INPUT_KEY_SELECT_ALL,
    INPUT_KEY_PASTE
} input_key;

/* The request for SDL keycode `key` with UI_MOD_* bits `mods` on `platform`
 * (UI_PLATFORM_*). */
input_key input_key_for(uint32_t key, uint16_t mods, uint8_t platform);

#endif
