#include "input.h"
#include "frame.h"

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

/* Text the box takes: valid UTF-8 with no control character but tab, newline
 * and the carriage return the insert drops. */
static bool allowed(uint32_t cp) {
    return cp == '\t' || cp == '\n' || cp == '\r' ||
           (cp >= 0x20 && cp != 0x7f && (cp < 0x80 || cp > 0x9f));
}

/* The byte offset `cols` characters on from `from`, stopping at a newline or
 * the end. The box holds valid UTF-8, so a step is one whole character. */
size_t input_advance(const input_box *in, size_t from, size_t cols) {
    sc_str t = input_text(in);
    size_t pos = from;
    for (size_t k = 0; k < cols && pos < in->len && at(in, pos) != '\n'; k++) {
        size_t step = sc_utf8_next(in->arena, t, pos).len;
        if (step == 0) {
            break;
        }
        pos += step;
    }
    return pos;
}

/* Characters between byte offsets `from` and `to` of one line. */
static size_t columns(const input_box *in, size_t from, size_t to) {
    return to > from ? sc_utf8_count(in->arena, sc_slice_sub(in->arena, in->bytes, from, to - from))
                     : 0;
}

input_status input_insert(input_box *in, const sc_arena *sa, sc_str s) {
    if (!live(in)) {
        return INPUT_STALE;
    }
    if (s.len > 0 && sc_slice_sub(sa, s, 0, s.len).len != s.len) {
        return INPUT_STALE;
    }
    size_t keep = 0; /* the bytes after CRs are dropped */
    for (size_t i = 0; i < s.len;) {
        sc_utf8_char c = sc_utf8_next(sa, s, i);
        if (!c.valid || !allowed(c.cp)) {
            return INPUT_NOT_TEXT;
        }
        keep += c.cp == '\r' ? 0u : c.len;
        i += c.len;
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
    if (in->cursor == 0) {
        return;
    }
    size_t prev = sc_utf8_prev(in->arena, input_text(in), in->cursor);
    if (!shift(in, in->cursor, prev, in->len - in->cursor)) {
        return;
    }
    in->len -= in->cursor - prev;
    in->cursor = prev;
}

void input_left(input_box *in) {
    in->all = false;
    if (in->cursor > 0 && live(in)) {
        in->cursor = sc_utf8_prev(in->arena, input_text(in), in->cursor);
    }
}

void input_right(input_box *in) {
    in->all = false;
    if (in->cursor < in->len && live(in)) {
        in->cursor += sc_utf8_next(in->arena, input_text(in), in->cursor).len;
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
    size_t col = columns(in, start, in->cursor);
    in->cursor = input_advance(in, line_start(in, start - 1), col);
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
    size_t col = columns(in, line_start(in, in->cursor), in->cursor);
    in->cursor = input_advance(in, end + 1, col);
}

void input_line_start(input_box *in) {
    in->all = false;
    if (live(in)) {
        in->cursor = line_start(in, in->cursor);
    }
}

void input_line_end(input_box *in) {
    in->all = false;
    if (live(in)) {
        in->cursor = line_end(in, in->cursor);
    }
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

input_key input_key_for(uint32_t key, uint16_t mods, uint8_t platform) {
    bool cmd = (mods & UI_MOD_CMD) != 0;
    bool mac_ctrl = frame_mac_ctrl(mods, platform) && !cmd;
    if (key >= 'A' && key <= 'Z') {
        key |= 0x20u; /* a keymap may report a shifted letter with Shift or Caps Lock */
    }
    switch (key) {
    case UI_KEY_RETURN:
    case UI_KEY_KP_ENTER:
        return cmd ? INPUT_KEY_NONE : INPUT_KEY_NEWLINE;
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
    case 'a':
        return cmd ? INPUT_KEY_SELECT_ALL : mac_ctrl ? INPUT_KEY_LINE_START : INPUT_KEY_NONE;
    case 'e':
        return mac_ctrl ? INPUT_KEY_LINE_END : INPUT_KEY_NONE;
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
    *column = columns(in, line_start(in, in->cursor), in->cursor);
}
