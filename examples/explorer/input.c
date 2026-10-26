#include "input.h"

static bool live(const input_box *in) {
    return sc_slice_sub(in->arena, in->bytes, 0, in->bytes.len).len == in->bytes.len;
}

/* The byte at `i`, or a newline past the end or when the box is stale. */
static uint8_t at(const input_box *in, size_t i) {
    uint8_t c = '\n';
    if (!sc_slice_get(in->arena, in->bytes, i, &c)) {
        return '\n';
    }
    return c;
}

static size_t line_start(const input_box *in, size_t pos) {
    while (pos > 0 && at(in, pos - 1) != '\n') {
        pos--;
    }
    return pos;
}

static size_t line_end(const input_box *in, size_t pos) {
    while (pos < in->len && at(in, pos) != '\n') {
        pos++;
    }
    return pos;
}

/* Moves bytes [from, len) to start at `to`. */
static bool shift(input_box *in, size_t from, size_t to, size_t n) {
    if (n == 0) {
        return true;
    }
    sc_slice src = sc_slice_sub(in->arena, in->bytes, from, n);
    sc_slice dst = sc_slice_sub(in->arena, in->bytes, to, n);
    return src.len == n && dst.len == n && sc_slice_move(in->arena, dst, src);
}

bool input_init(input_box *in, sc_arena *a, size_t cap) {
    in->arena = a;
    in->len = 0;
    in->cursor = 0;
    in->all = false;
    in->bytes = sc_arena_alloc(a, cap);
    return cap > 0 && in->bytes.len == cap;
}

static bool allowed(uint8_t c) {
    return c == '\t' || c == '\n' || c == '\r' || (c >= 0x20 && c <= 0x7e);
}

input_status input_insert(input_box *in, const sc_arena *sa, sc_str s) {
    if (!live(in)) {
        return INPUT_STALE;
    }
    if (s.len > 0 && sc_slice_sub(sa, s, 0, s.len).len != s.len) {
        return INPUT_STALE;
    }
    size_t keep = 0; /* the bytes after CRs are dropped */
    for (size_t i = 0; i < s.len; i++) {
        uint8_t c = 0;
        if (!sc_slice_get(sa, s, i, &c) || !allowed(c)) {
            return INPUT_NOT_ASCII;
        }
        keep += c == '\r' ? 0u : 1u;
    }
    size_t base = in->all ? 0 : in->len;
    size_t pos = in->all ? 0 : in->cursor;
    if (keep > in->bytes.len - base) {
        return INPUT_TOO_BIG;
    }
    if (!shift(in, pos, pos + keep, base - pos)) {
        return INPUT_STALE;
    }
    if (keep > 0) {
        sc_buf b = sc_buf_new(sc_slice_sub(in->arena, in->bytes, pos, keep));
        for (size_t i = 0; i < s.len; i++) {
            uint8_t c = 0;
            if (!sc_slice_get(sa, s, i, &c) || (c != '\r' && !sc_buf_lit(in->arena, &b, &c, 1))) {
                return INPUT_STALE;
            }
        }
    }
    in->len = base + keep;
    in->cursor = pos + keep;
    in->all = false;
    return INPUT_OK;
}

void input_backspace(input_box *in) {
    if (!live(in)) {
        return;
    }
    if (in->all) {
        in->len = 0;
        in->cursor = 0;
        in->all = false;
        return;
    }
    if (in->cursor == 0 || !shift(in, in->cursor, in->cursor - 1, in->len - in->cursor)) {
        return;
    }
    in->cursor--;
    in->len--;
}

void input_left(input_box *in) {
    in->all = false;
    if (in->cursor > 0) {
        in->cursor--;
    }
}

void input_right(input_box *in) {
    in->all = false;
    if (in->cursor < in->len) {
        in->cursor++;
    }
}

void input_up(input_box *in) {
    in->all = false;
    if (!live(in)) {
        return;
    }
    size_t start = line_start(in, in->cursor);
    if (start == 0) {
        in->cursor = 0;
        return;
    }
    size_t col = in->cursor - start;
    size_t prev_end = start - 1;
    size_t prev_start = line_start(in, prev_end);
    size_t width = prev_end - prev_start;
    in->cursor = prev_start + (col < width ? col : width);
}

void input_down(input_box *in) {
    in->all = false;
    if (!live(in)) {
        return;
    }
    size_t end = line_end(in, in->cursor);
    if (end == in->len) {
        in->cursor = in->len;
        return;
    }
    size_t col = in->cursor - line_start(in, in->cursor);
    size_t next_start = end + 1;
    size_t width = line_end(in, next_start) - next_start;
    in->cursor = next_start + (col < width ? col : width);
}

void input_select_all(input_box *in) {
    in->all = true;
}

sc_str input_text(const input_box *in) {
    sc_str empty = {NULL, 0, in->arena->epoch};
    if (in->len == 0) {
        return empty;
    }
    return sc_slice_sub(in->arena, in->bytes, 0, in->len);
}

input_key input_key_for(uint32_t key, uint16_t mods) {
    bool cmd = (mods & (UI_MOD_GUI | UI_MOD_CTRL)) != 0;
    if (key >= 'A' && key <= 'Z') {
        key |= 0x20u; /* a keymap may report a shifted letter with Shift or Caps Lock */
    }
    switch (key) {
    case UI_KEY_RETURN:
    case UI_KEY_KP_ENTER:
        return cmd ? INPUT_KEY_RUN : INPUT_KEY_NEWLINE;
    case UI_KEY_TAB:
        return INPUT_KEY_TAB;
    case UI_KEY_BACKSPACE:
        return INPUT_KEY_BACKSPACE;
    case UI_KEY_LEFT:
        return INPUT_KEY_LEFT;
    case UI_KEY_RIGHT:
        return INPUT_KEY_RIGHT;
    case UI_KEY_UP:
        return INPUT_KEY_UP;
    case UI_KEY_DOWN:
        return INPUT_KEY_DOWN;
    case UI_KEY_ESCAPE:
        return INPUT_KEY_CANCEL;
    case 'a':
        return cmd ? INPUT_KEY_SELECT_ALL : INPUT_KEY_NONE;
    case 'v':
        return cmd ? INPUT_KEY_PASTE : INPUT_KEY_NONE;
    default:
        return INPUT_KEY_NONE;
    }
}

void input_cursor_at(const input_box *in, size_t *line, size_t *column) {
    size_t l = 0;
    for (size_t i = 0; i < in->cursor; i++) {
        if (at(in, i) == '\n') {
            l++;
        }
    }
    *line = l;
    *column = in->cursor - line_start(in, in->cursor);
}
