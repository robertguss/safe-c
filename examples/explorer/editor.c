#include "editor.h"
#include "frame.h"

#define STEP_TYPE 1u
#define STEP_BACK 2u
#define STEP_DEL 3u
#define STEP_OTHER 4u
#define NO_STORE SIZE_MAX

static size_t gap_len(const editor *e) {
    return e->gap_end - e->gap;
}

size_t editor_len(const editor *e) {
    return e->text.len - gap_len(e);
}

static size_t phys(const editor *e, size_t i) {
    return i < e->gap ? i : i + gap_len(e);
}

bool editor_byte(const editor *e, size_t i, uint8_t *out) {
    return i < editor_len(e) && sc_slice_get(e->arena, e->text, phys(e, i), out);
}

static uint8_t at(const editor *e, size_t i) {
    uint8_t c = '\n';
    return editor_byte(e, i, &c) ? c : (uint8_t)'\n';
}

static bool cont(uint8_t c) {
    return (c & 0xC0u) == 0x80u;
}

bool editor_move(const sc_arena *a, sc_slice s, size_t to, size_t from, size_t n) {
    if (n == 0) {
        return true;
    }
    sc_slice d = sc_slice_sub(a, s, to, n);
    sc_slice src = sc_slice_sub(a, s, from, n);
    return d.len == n && src.len == n && sc_slice_move(a, d, src);
}

static bool move_gap(editor *e, size_t pos) {
    if (pos < e->gap) {
        size_t n = e->gap - pos;
        if (!editor_move(e->arena, e->text, e->gap_end - n, pos, n)) {
            return false;
        }
        e->gap = pos;
        e->gap_end -= n;
    } else if (pos > e->gap) {
        size_t n = pos - e->gap;
        if (!editor_move(e->arena, e->text, e->gap, e->gap_end, n)) {
            return false;
        }
        e->gap += n;
        e->gap_end += n;
    }
    return true;
}

static bool copy_range(const editor *e, const sc_arena *da, sc_buf *b, size_t from, size_t to) {
    size_t mid = to < e->gap ? to : e->gap;
    if (from < mid && !sc_buf_str(da, b, e->arena, sc_slice_sub(e->arena, e->text, from, mid - from))) {
        return false;
    }
    size_t rest = from > e->gap ? from : e->gap;
    return rest >= to ||
           sc_buf_str(da, b, e->arena, sc_slice_sub(e->arena, e->text, phys(e, rest), to - rest));
}

sc_str editor_copy(const editor *e, size_t from, size_t to, sc_arena *dst) {
    sc_str empty = {NULL, 0, dst->epoch};
    if (to > editor_len(e) || from >= to) {
        return empty;
    }
    sc_buf b = sc_buf_new(sc_arena_alloc(dst, to - from));
    return copy_range(e, dst, &b, from, to) ? sc_buf_view(dst, b) : empty;
}

static bool step_get(const editor *e, size_t k, editor_step *out) {
    return SC_TSLICE_GET(editor_step, e->arena, e->steps, (e->first + k) % EDITOR_UNDO_STEPS, out);
}

static bool step_set(editor *e, size_t k, const editor_step *s) {
    return SC_TSLICE_SET(editor_step, e->arena, e->steps, (e->first + k) % EDITOR_UNDO_STEPS, s);
}

bool editor_clear(editor *e) {
    e->gap = 0;
    e->gap_end = e->text.len;
    e->cursor = 0;
    e->anchor = 0;
    e->store_used = 0;
    e->first = 0;
    e->count = 0;
    e->done = 0;
    e->sealed = true;
    e->lines = 1;
    e->top = 0;
    e->top_off = 0;
    e->left = 0;
    e->follow = true;
    e->compose_len = 0;
    e->compose_caret = 0;
    e->version++;
    e->stmt_known = false;
    editor_checkpoint zero = {0, 0, 0, 0, 0, 0};
    e->cp_count = 1;
    return SC_TSLICE_SET(editor_checkpoint, e->arena, e->cps, 0, &zero);
}

bool editor_init(editor *e, sc_arena *a, sc_arena *scratch, bool std) {
    e->arena = a;
    e->scratch = scratch;
    e->std_strings = std;
    e->dialect = SQLTOK_POSTGRESQL;
    e->version = 0;
    e->relexed = 0;
    e->text = sc_arena_alloc(a, EDITOR_CAP);
    e->store = sc_arena_alloc(a, EDITOR_UNDO_BYTES);
    e->steps = SC_TSLICE_ALLOC(editor_step, a, EDITOR_UNDO_STEPS);
    e->cps = SC_TSLICE_ALLOC(editor_checkpoint, a, EDITOR_CHECKPOINTS);
    e->tags = sc_arena_alloc(a, EDITOR_CHECKPOINTS * EDITOR_TAG_MAX);
    e->tag_live = sc_arena_alloc(a, EDITOR_TAG_MAX);
    e->compose = sc_arena_alloc(a, EDITOR_COMPOSE_CAP);
    return e->text.len == EDITOR_CAP && e->store.len == EDITOR_UNDO_BYTES &&
           sc_tslice_len(e->steps) == EDITOR_UNDO_STEPS && sc_tslice_len(e->cps) == EDITOR_CHECKPOINTS &&
           e->tags.len == EDITOR_CHECKPOINTS * EDITOR_TAG_MAX && e->tag_live.len == EDITOR_TAG_MAX &&
           e->compose.len == EDITOR_COMPOSE_CAP && editor_clear(e);
}

static size_t newlines(const editor *e, size_t from, size_t to) {
    size_t n = 0;
    for (size_t i = from; i < to; i++) {
        n += at(e, i) == '\n' ? 1u : 0u;
    }
    return n;
}

static bool to_store(editor *e, size_t store, size_t from, size_t n) {
    if (n == 0) {
        return true;
    }
    sc_slice d = sc_slice_sub(e->arena, e->store, store, n);
    sc_slice s = sc_slice_sub(e->arena, e->text, from, n);
    return d.len == n && s.len == n && sc_slice_copy(e->arena, d, s);
}

static bool put_text(editor *e, const sc_arena *sa, sc_str s, size_t keep) {
    sc_buf b = sc_buf_new(sc_slice_sub(e->arena, e->text, e->gap, keep));
    size_t run = 0;
    for (size_t i = 0; i <= s.len; i++) {
        uint8_t c = '\r';
        if (i < s.len && !sc_slice_get(sa, s, i, &c)) {
            return false;
        }
        if (c != '\r') {
            continue;
        }
        if (i > run && !sc_buf_str(e->arena, &b, sa, sc_slice_sub(sa, s, run, i - run))) {
            return false;
        }
        run = i + 1u;
    }
    return sc_buf_view(e->arena, b).len == keep;
}

static bool edit(editor *e, size_t from, size_t to, const sc_arena *sa, sc_str s, size_t keep, size_t store) {
    size_t removed = to - from;
    if (!move_gap(e, from)) {
        return false;
    }
    size_t gone = newlines(e, from, to);
    if (store != NO_STORE && !to_store(e, store, e->gap_end, removed)) {
        return false;
    }
    e->gap_end += removed;
    if (keep > 0 && (!put_text(e, sa, s, keep) || (store != NO_STORE && !to_store(e, store + removed, e->gap, keep)))) {
        e->gap_end -= removed;
        return false;
    }
    e->gap += keep;
    e->lines = e->lines - gone + newlines(e, from, from + keep);
    if (from < e->top_off) {
        e->top_off = editor_line_start(e, from);
        e->top = newlines(e, 0, e->top_off);
    }
    e->version++;
    e->follow = true;
    e->stmt_known = false;
    editor_relex(e, from, removed, keep);
    return true;
}

static size_t step_end(const editor_step *s) {
    return s->bytes + s->removed + s->inserted;
}

static void drop_oldest(editor *e) {
    e->first = (e->first + 1u) % EDITOR_UNDO_STEPS;
    e->count--;
    e->done--;
}

static size_t reserve(editor *e, size_t n) {
    editor_step s;
    e->count = e->done;
    e->store_used = e->count > 0 && step_get(e, e->count - 1u, &s) ? step_end(&s) : 0u;
    if (n > e->store.len) {
        e->first = 0;
        e->count = 0;
        e->done = 0;
        e->store_used = 0;
        return NO_STORE;
    }
    while (e->count > 0 && step_get(e, 0, &s) &&
           (e->count >= EDITOR_UNDO_STEPS || e->store_used - s.bytes > e->store.len - n)) {
        drop_oldest(e);
    }
    if (e->count == 0) {
        e->store_used = 0;
    } else if (step_get(e, 0, &s) && s.bytes > 0) {
        size_t base = s.bytes;
        if (!editor_move(e->arena, e->store, 0, base, e->store_used - base)) {
            return NO_STORE;
        }
        for (size_t k = 0; k < e->count; k++) {
            if (step_get(e, k, &s)) {
                s.bytes -= base;
                (void)step_set(e, k, &s);
            }
        }
        e->store_used -= base;
    }
    size_t off = e->store_used;
    e->store_used += n;
    return off;
}

static bool joins(const editor *e, size_t from, size_t to, size_t keep, uint8_t kind, uint64_t ms,
                  editor_step *last) {
    if (e->sealed || kind == STEP_OTHER || e->done == 0 || e->done != e->count ||
        !step_get(e, e->done - 1u, last) || last->kind != kind || ms < last->ms ||
        ms - last->ms >= EDITOR_GROUP_MS || step_end(last) != e->store_used) {
        return false;
    }
    size_t room = e->store.len - e->store_used;
    if (kind == STEP_TYPE) {
        return from == to && from == last->at + last->inserted && keep <= room;
    }
    return keep == 0 && to - from <= room && (kind == STEP_BACK ? to == last->at : from == last->at);
}

static bool prepend(editor *e, const editor_step *last, size_t k) {
    sc_slice tmp;
    if (!sc_arena_reset(e->scratch)) {
        return false;
    }
    tmp = sc_arena_alloc(e->scratch, k);
    sc_buf b = sc_buf_new(tmp);
    size_t old = last->removed;
    sc_buf back = sc_buf_new(sc_slice_sub(e->arena, e->store, last->bytes, k));
    return tmp.len == k &&
           sc_buf_str(e->scratch, &b, e->arena, sc_slice_sub(e->arena, e->store, last->bytes + old, k)) &&
           editor_move(e->arena, e->store, last->bytes + k, last->bytes, old) &&
           sc_buf_str(e->arena, &back, e->scratch, sc_buf_view(e->scratch, b));
}

static editor_status replace(editor *e, size_t from, size_t to, const sc_arena *sa, sc_str s, size_t keep,
                             uint8_t kind, uint64_t ms) {
    if (keep > EDITOR_CAP - (editor_len(e) - (to - from))) {
        return EDITOR_TOO_BIG;
    }
    editor_step last;
    size_t before_c = e->cursor, before_a = e->anchor;
    if (joins(e, from, to, keep, kind, ms, &last)) {
        size_t store = e->store_used;
        if (!edit(e, from, to, sa, s, keep, store)) {
            return EDITOR_STALE;
        }
        e->store_used += (to - from) + keep;
        if (kind == STEP_BACK && !prepend(e, &last, to - from)) {
            return EDITOR_STALE;
        }
        last.at = kind == STEP_BACK ? from : last.at;
        last.removed += to - from;
        last.inserted += keep;
        last.ms = ms;
        last.after = from + keep;
        (void)step_set(e, e->done - 1u, &last);
    } else {
        size_t store = reserve(e, (to - from) + keep);
        if (!edit(e, from, to, sa, s, keep, store)) {
            return EDITOR_STALE;
        }
        if (store != NO_STORE) {
            editor_step st = {from, to - from, keep, store, before_c, before_a, from + keep, ms, kind};
            (void)step_set(e, e->count, &st);
            e->count++;
            e->done++;
        }
    }
    e->cursor = from + keep;
    e->anchor = e->cursor;
    e->sealed = kind == STEP_OTHER;
    return EDITOR_OK;
}

static bool undo(editor *e) {
    editor_step s;
    if (e->done == 0 || !step_get(e, e->done - 1u, &s) ||
        !edit(e, s.at, s.at + s.inserted, e->arena, sc_slice_sub(e->arena, e->store, s.bytes, s.removed),
              s.removed, NO_STORE)) {
        return false;
    }
    e->done--;
    e->cursor = s.cursor;
    e->anchor = s.anchor;
    e->sealed = true;
    return true;
}

static bool redo(editor *e) {
    editor_step s;
    if (e->done >= e->count || !step_get(e, e->done, &s) ||
        !edit(e, s.at, s.at + s.removed, e->arena,
              sc_slice_sub(e->arena, e->store, s.bytes + s.removed, s.inserted), s.inserted, NO_STORE)) {
        return false;
    }
    e->done++;
    e->cursor = s.after;
    e->anchor = s.after;
    e->sealed = true;
    return true;
}

static bool allowed(uint32_t cp) {
    return cp == '\t' || cp == '\n' || cp == '\r' || (cp >= 0x20 && cp != 0x7f && (cp < 0x80 || cp > 0x9f));
}

static editor_status scan_insert(const sc_arena *sa, sc_str s, size_t *keep) {
    *keep = 0;
    if (s.len > 0 && sc_slice_sub(sa, s, 0, s.len).len != s.len) {
        return EDITOR_STALE;
    }
    for (size_t i = 0; i < s.len;) {
        sc_utf8_char c = sc_utf8_next(sa, s, i);
        if (!c.valid || !allowed(c.cp)) {
            return EDITOR_NOT_TEXT;
        }
        *keep += c.cp == '\r' ? 0u : c.len;
        i += c.len;
    }
    return EDITOR_OK;
}

void editor_selection(const editor *e, size_t *from, size_t *to) {
    *from = e->cursor < e->anchor ? e->cursor : e->anchor;
    *to = e->cursor < e->anchor ? e->anchor : e->cursor;
}

editor_status editor_insert(editor *e, const sc_arena *sa, sc_str s, bool typed, uint64_t ms) {
    size_t keep = 0;
    editor_status st = scan_insert(sa, s, &keep);
    if (st != EDITOR_OK) {
        return st;
    }
    bool commit = e->compose_len > 0;
    e->compose_len = 0;
    size_t from = 0, to = 0;
    editor_selection(e, &from, &to);
    if (from == to && keep == 0) {
        return EDITOR_OK;
    }
    return replace(e, from, to, sa, s, keep, typed && !commit ? STEP_TYPE : STEP_OTHER, ms);
}

static size_t prev_cp(const editor *e, size_t pos) {
    if (pos == 0) {
        return 0;
    }
    pos--;
    while (pos > 0 && cont(at(e, pos))) {
        pos--;
    }
    return pos;
}

static size_t next_cp(const editor *e, size_t pos) {
    size_t len = editor_len(e);
    if (pos >= len) {
        return len;
    }
    pos++;
    while (pos < len && cont(at(e, pos))) {
        pos++;
    }
    return pos;
}

static size_t snap(const editor *e, size_t pos) {
    size_t len = editor_len(e);
    pos = pos < len ? pos : len;
    while (pos > 0 && pos < len && cont(at(e, pos))) {
        pos--;
    }
    return pos;
}

size_t editor_line_start(const editor *e, size_t off) {
    while (off > 0 && at(e, off - 1u) != '\n') {
        off--;
    }
    return off;
}

size_t editor_line_end(const editor *e, size_t off) {
    size_t len = editor_len(e);
    while (off < len && at(e, off) != '\n') {
        off++;
    }
    return off;
}

size_t editor_columns(const editor *e, size_t from, size_t to) {
    size_t n = 0;
    for (size_t i = from; i < to; i++) {
        n += cont(at(e, i)) ? 0u : 1u;
    }
    return n;
}

size_t editor_advance(const editor *e, size_t from, size_t cols) {
    size_t len = editor_len(e);
    for (size_t k = 0; k < cols && from < len && at(e, from) != '\n'; k++) {
        from = next_cp(e, from);
    }
    return from;
}

size_t editor_line_of(const editor *e, size_t off) {
    return off >= e->top_off ? e->top + newlines(e, e->top_off, off) : e->top - newlines(e, off, e->top_off);
}

size_t editor_line_after_top(const editor *e, size_t k) {
    size_t off = e->top_off;
    size_t len = editor_len(e);
    for (size_t i = 0; i < k; i++) {
        size_t end = editor_line_end(e, off);
        if (end >= len) {
            break;
        }
        off = end + 1u;
    }
    return off;
}

static bool word(uint8_t c) {
    uint8_t l = (uint8_t)(c | 0x20u);
    return c == '_' || c >= 0x80u || (c >= '0' && c <= '9') || (l >= 'a' && l <= 'z');
}

static size_t word_right(const editor *e, size_t pos) {
    size_t len = editor_len(e);
    while (pos < len && !word(at(e, pos))) {
        pos++;
    }
    while (pos < len && word(at(e, pos))) {
        pos++;
    }
    return pos;
}

static size_t word_left(const editor *e, size_t pos) {
    while (pos > 0 && !word(at(e, pos - 1u))) {
        pos--;
    }
    while (pos > 0 && word(at(e, pos - 1u))) {
        pos--;
    }
    return pos;
}

static size_t up(const editor *e, size_t pos) {
    size_t start = editor_line_start(e, pos);
    if (start == 0) {
        return 0;
    }
    return editor_advance(e, editor_line_start(e, start - 1u), editor_columns(e, start, pos));
}

static size_t down(const editor *e, size_t pos) {
    size_t end = editor_line_end(e, pos);
    if (end >= editor_len(e)) {
        return editor_len(e);
    }
    return editor_advance(e, end + 1u, editor_columns(e, editor_line_start(e, pos), pos));
}

void editor_select(editor *e, size_t anchor, size_t cursor) {
    e->anchor = snap(e, anchor);
    e->cursor = snap(e, cursor);
    e->sealed = true;
    e->follow = true;
}

void editor_select_word(editor *e, size_t off) {
    size_t len = editor_len(e);
    off = snap(e, off);
    if (off == len && off > 0) {
        off = prev_cp(e, off);
    }
    if (off >= len || !word(at(e, off))) {
        editor_select(e, off, next_cp(e, off));
        return;
    }
    size_t from = off, to = off;
    while (from > 0 && word(at(e, from - 1u))) {
        from--;
    }
    while (to < len && word(at(e, to))) {
        to++;
    }
    editor_select(e, from, to);
}

void editor_select_line(editor *e, size_t off) {
    size_t end = editor_line_end(e, off);
    editor_select(e, editor_line_start(e, off), end < editor_len(e) ? end + 1u : end);
}

static bool move_to(editor *e, size_t pos, bool extend) {
    size_t c = e->cursor, a = e->anchor;
    e->cursor = pos;
    e->anchor = extend ? e->anchor : pos;
    e->sealed = true;
    e->follow = true;
    return c != e->cursor || a != e->anchor;
}

static bool erase(editor *e, bool forward, uint64_t ms) {
    size_t from = 0, to = 0;
    editor_selection(e, &from, &to);
    uint8_t kind = STEP_OTHER;
    if (from == to) {
        from = forward ? e->cursor : prev_cp(e, e->cursor);
        to = forward ? next_cp(e, e->cursor) : e->cursor;
        kind = forward ? STEP_DEL : STEP_BACK;
    }
    sc_str none = {NULL, 0, e->arena->epoch};
    return from < to && replace(e, from, to, e->arena, none, 0, kind, ms) == EDITOR_OK;
}

static void line_change(const editor *e, editor_cmd cmd, bool uncomment, size_t ls, size_t le, size_t *pre,
                        size_t *drop) {
    *pre = 0;
    *drop = 0;
    if (cmd == EDITOR_CMD_COMMENT && uncomment) {
        *drop = le == ls ? 0u : le - ls >= 3u && at(e, ls + 2u) == ' ' ? 3u : 2u;
    } else if (cmd == EDITOR_CMD_OUTDENT) {
        while (*drop < 2u && ls + *drop < le && at(e, ls + *drop) == ' ') {
            (*drop)++;
        }
    } else if (le > ls) {
        *pre = cmd == EDITOR_CMD_COMMENT ? 3u : 2u;
    }
}

static bool line_op(editor *e, editor_cmd cmd, uint64_t ms) {
    size_t s0 = 0, s1 = 0;
    editor_selection(e, &s0, &s1);
    bool sel = s1 > s0;
    size_t from = editor_line_start(e, s0);
    size_t last = sel && s1 > from && at(e, s1 - 1u) == '\n' ? s1 - 1u : s1;
    size_t to = editor_line_end(e, last);
    bool any = false, all = true;
    for (size_t ls = from;;) {
        size_t le = editor_line_end(e, ls);
        if (le > ls) {
            any = true;
            all = all && le - ls >= 2u && at(e, ls) == '-' && at(e, ls + 1u) == '-';
        }
        if (le >= to) {
            break;
        }
        ls = le + 1u;
    }
    bool uncomment = any && all;
    size_t grown = 0, cut = 0, pre0 = 0, drop0 = 0;
    for (size_t ls = from;;) {
        size_t le = editor_line_end(e, ls), pre = 0, drop = 0;
        line_change(e, cmd, uncomment, ls, le, &pre, &drop);
        pre0 = ls == from ? pre : pre0;
        drop0 = ls == from ? drop : drop0;
        grown += pre;
        cut += drop;
        if (le >= to) {
            break;
        }
        ls = le + 1u;
    }
    size_t n = to - from + grown - cut;
    if ((grown == 0 && cut == 0) || n > EDITOR_CAP - (editor_len(e) - (to - from)) ||
        !sc_arena_reset(e->scratch)) {
        return false;
    }
    sc_buf b = sc_buf_new(sc_arena_alloc(e->scratch, n));
    for (size_t ls = from;;) {
        size_t le = editor_line_end(e, ls), pre = 0, drop = 0;
        line_change(e, cmd, uncomment, ls, le, &pre, &drop);
        if ((pre == 3u && !SC_BUF_LIT(e->scratch, &b, "-- ")) || (pre == 2u && !SC_BUF_LIT(e->scratch, &b, "  ")) ||
            !copy_range(e, e->scratch, &b, ls + drop, le < to ? le + 1u : le)) {
            return false;
        }
        if (le >= to) {
            break;
        }
        ls = le + 1u;
    }
    size_t c = e->cursor;
    sc_str text = sc_buf_view(e->scratch, b);
    if (text.len != n || replace(e, from, to, e->scratch, text, n, STEP_OTHER, ms) != EDITOR_OK) {
        return false;
    }
    if (sel) {
        e->anchor = from;
        e->cursor = from + n;
    } else {
        e->cursor = pre0 > 0 ? c + pre0 : c - (drop0 < c - from ? drop0 : c - from);
        e->anchor = e->cursor;
    }
    return true;
}

static editor_press press(editor_cmd cmd, bool extend) {
    editor_press p = {cmd, extend};
    return p;
}

editor_press editor_press_for(uint32_t key, uint16_t mods, uint8_t platform) {
    bool cmd = frame_cmd(mods, platform);
    bool alt = (mods & UI_MOD_ALT) != 0;
    bool shift = (mods & UI_MOD_SHIFT) != 0;
    bool mac_ctrl = frame_mac_ctrl(mods, platform) && (mods & UI_MOD_CMD) == 0;
    if (key >= 'A' && key <= 'Z') {
        key |= 0x20u;
    }
    switch (key) {
    case UI_KEY_RETURN:
    case UI_KEY_KP_ENTER:
        return press(cmd ? EDITOR_CMD_NONE : EDITOR_CMD_NEWLINE, false);
    case UI_KEY_TAB:
        return press(cmd || alt ? EDITOR_CMD_NONE : EDITOR_CMD_TAB, false);
    case UI_KEY_BACKSPACE:
        return press(EDITOR_CMD_BACKSPACE, false);
    case UI_KEY_DELETE:
        return press(EDITOR_CMD_DELETE, false);
    case UI_KEY_LEFT:
        return press(cmd ? EDITOR_CMD_LINE_START : alt ? EDITOR_CMD_WORD_LEFT : EDITOR_CMD_LEFT, shift);
    case UI_KEY_RIGHT:
        return press(cmd ? EDITOR_CMD_LINE_END : alt ? EDITOR_CMD_WORD_RIGHT : EDITOR_CMD_RIGHT, shift);
    case UI_KEY_UP:
        return press(cmd ? EDITOR_CMD_DOC_START : EDITOR_CMD_UP, shift);
    case UI_KEY_DOWN:
        return press(cmd ? EDITOR_CMD_DOC_END : EDITOR_CMD_DOWN, shift);
    case UI_KEY_HOME:
        return press(EDITOR_CMD_LINE_START, shift);
    case UI_KEY_END:
        return press(EDITOR_CMD_LINE_END, shift);
    case UI_KEY_ESCAPE:
        return press(EDITOR_CMD_ESCAPE, false);
    case 'a':
        return press(cmd ? EDITOR_CMD_SELECT_ALL : mac_ctrl ? EDITOR_CMD_LINE_START : EDITOR_CMD_NONE, false);
    case 'e':
        return press(mac_ctrl ? EDITOR_CMD_LINE_END : EDITOR_CMD_NONE, false);
    case 'z':
        return press(!cmd ? EDITOR_CMD_NONE : shift ? EDITOR_CMD_REDO : EDITOR_CMD_UNDO, false);
    case 'c':
        return press(cmd ? EDITOR_CMD_COPY : EDITOR_CMD_NONE, false);
    case 'x':
        return press(cmd ? EDITOR_CMD_CUT : EDITOR_CMD_NONE, false);
    case 'v':
        return press(cmd ? EDITOR_CMD_PASTE : EDITOR_CMD_NONE, false);
    case '/':
        return press(cmd ? EDITOR_CMD_COMMENT : EDITOR_CMD_NONE, false);
    case ']':
        return press(cmd ? EDITOR_CMD_INDENT : EDITOR_CMD_NONE, false);
    case '[':
        return press(cmd ? EDITOR_CMD_OUTDENT : EDITOR_CMD_NONE, false);
    default:
        return press(EDITOR_CMD_NONE, false);
    }
}

static editor_status literal(editor *e, bool two, uint64_t ms) {
    if (!sc_arena_reset(e->scratch)) {
        return EDITOR_STALE;
    }
    sc_str s = two ? SC_STR_LIT(e->scratch, "  ") : SC_STR_LIT(e->scratch, "\n");
    return editor_insert(e, e->scratch, s, true, ms);
}

bool editor_do(editor *e, editor_press p, uint64_t ms) {
    size_t from = 0, to = 0;
    editor_selection(e, &from, &to);
    bool x = p.extend;
    bool collapse = from < to && !x;
    switch (p.cmd) {
    case EDITOR_CMD_NEWLINE:
        return literal(e, false, ms) == EDITOR_OK;
    case EDITOR_CMD_TAB:
        return literal(e, true, ms) == EDITOR_OK;
    case EDITOR_CMD_BACKSPACE:
        return erase(e, false, ms);
    case EDITOR_CMD_DELETE:
        return erase(e, true, ms);
    case EDITOR_CMD_LEFT:
        return move_to(e, collapse ? from : prev_cp(e, e->cursor), x);
    case EDITOR_CMD_RIGHT:
        return move_to(e, collapse ? to : next_cp(e, e->cursor), x);
    case EDITOR_CMD_UP:
        return move_to(e, up(e, e->cursor), x);
    case EDITOR_CMD_DOWN:
        return move_to(e, down(e, e->cursor), x);
    case EDITOR_CMD_WORD_LEFT:
        return move_to(e, word_left(e, e->cursor), x);
    case EDITOR_CMD_WORD_RIGHT:
        return move_to(e, word_right(e, e->cursor), x);
    case EDITOR_CMD_LINE_START:
        return move_to(e, editor_line_start(e, e->cursor), x);
    case EDITOR_CMD_LINE_END:
        return move_to(e, editor_line_end(e, e->cursor), x);
    case EDITOR_CMD_DOC_START:
        return move_to(e, 0, x);
    case EDITOR_CMD_DOC_END:
        return move_to(e, editor_len(e), x);
    case EDITOR_CMD_SELECT_ALL:
        editor_select(e, 0, editor_len(e));
        return from > 0 || to < editor_len(e);
    case EDITOR_CMD_UNDO:
        return undo(e);
    case EDITOR_CMD_REDO:
        return redo(e);
    case EDITOR_CMD_COMMENT:
    case EDITOR_CMD_INDENT:
    case EDITOR_CMD_OUTDENT:
        return line_op(e, p.cmd, ms);
    case EDITOR_CMD_ESCAPE:
        if (e->compose_len == 0) {
            return false;
        }
        e->compose_len = 0;
        return true;
    default:
        return false;
    }
}

void editor_compose(editor *e, const sc_arena *sa, sc_str s, size_t caret) {
    sc_str cut = sc_utf8_truncate(sa, s, EDITOR_COMPOSE_CAP);
    sc_buf b = sc_buf_new(e->compose);
    e->compose_len = cut.len > 0 && sc_buf_str(e->arena, &b, sa, cut) ? cut.len : 0u;
    e->compose_caret = caret < e->compose_len ? caret : e->compose_len;
}

sc_str editor_compose_text(const editor *e) {
    sc_str empty = {NULL, 0, e->arena->epoch};
    return e->compose_len > 0 ? sc_slice_sub(e->arena, e->compose, 0, e->compose_len) : empty;
}

void editor_scroll(editor *e, size_t rows, size_t cols) {
    if (!e->follow) {
        return;
    }
    e->follow = false;
    size_t line = editor_line_of(e, e->cursor);
    if (line < e->top) {
        e->top = line;
        e->top_off = editor_line_start(e, e->cursor);
    } else if (rows > 0 && line >= e->top + rows) {
        size_t k = line - (rows - 1u) - e->top;
        e->top_off = editor_line_after_top(e, k);
        e->top += k;
    }
    size_t col = editor_columns(e, editor_line_start(e, e->cursor), e->cursor);
    if (col < e->left) {
        e->left = col;
    } else if (cols > 0 && col >= e->left + cols) {
        e->left = col - cols + 1u;
    }
}

bool editor_wheel(editor *e, int32_t lines) {
    size_t was = e->top;
    if (lines > 0) {
        size_t off = editor_line_after_top(e, (size_t)lines);
        e->top += newlines(e, e->top_off, off);
        e->top_off = off;
    }
    for (int32_t i = 0; i > lines && e->top_off > 0; i--) {
        e->top_off = editor_line_start(e, e->top_off - 1u);
        e->top--;
    }
    return was != e->top;
}
