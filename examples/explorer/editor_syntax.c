#include "editor.h"

static bool get_cp(const editor *e, size_t i, editor_checkpoint *out) {
    return i < e->cp_count && SC_TSLICE_GET(editor_checkpoint, e->arena, e->cps, i, out);
}

static sc_slice slot(const editor *e, size_t i, size_t n) {
    return sc_slice_sub(e->arena, e->tags, i * EDITOR_TAG_MAX, n);
}

static sqltok_state load(editor *e, size_t i) {
    sqltok_state st = {0};
    editor_checkpoint c;
    sc_buf b = sc_buf_new(e->tag_live);
    if (get_cp(e, i, &c) && sc_buf_str(e->arena, &b, e->arena, slot(e, i, c.tag_len))) {
        st.mode = c.mode;
        st.depth = c.depth;
        st.quote = c.quote;
        st.tag = sc_buf_view(e->arena, b);
    }
    st.tag_arena = e->arena;
    st.std_strings = e->std_strings;
    st.dialect = e->dialect;
    return st;
}

static bool keep_tag(editor *e, sqltok_state *st) {
    if (st->tag_arena != e->scratch) {
        return true;
    }
    sc_buf b = sc_buf_new(e->tag_live);
    if (st->tag.len > EDITOR_TAG_MAX || !sc_buf_str(e->arena, &b, e->scratch, st->tag)) {
        return false;
    }
    st->tag = sc_buf_view(e->arena, b);
    st->tag_arena = e->arena;
    return true;
}

static bool window(editor *e, size_t off, size_t end, sc_str *w) {
    if (!sc_arena_reset(e->scratch)) {
        return false;
    }
    *w = editor_copy(e, off, end, e->scratch);
    return w->len == end - off;
}

static size_t run_window(const sc_arena *a, sc_str w, bool ae, sqltok_state *st) {
    size_t k = 0;
    for (;;) {
        sqltok_token t = sqltok_step(a, w, k, *st, ae);
        if (t.kind == SQLTOK_NONE) {
            return k;
        }
        k = t.end;
        *st = t.next;
    }
}

static bool same(const editor *e, size_t i, const sqltok_state *st) {
    editor_checkpoint c;
    if (!get_cp(e, i, &c) || c.mode != st->mode || c.depth != st->depth || c.quote != st->quote ||
        c.tag_len != st->tag.len) {
        return false;
    }
    sc_str t = slot(e, i, c.tag_len);
    for (size_t k = 0; k < c.tag_len; k++) {
        uint8_t x = 0, y = 1;
        if (!sc_slice_get(e->arena, t, k, &x) || !sc_slice_get(st->tag_arena, st->tag, k, &y) || x != y) {
            return false;
        }
    }
    return true;
}

static bool cp_shift(editor *e, size_t to, size_t from, size_t n) {
    size_t es = sizeof(editor_checkpoint);
    return editor_move(e->arena, e->cps.bytes, to * es, from * es, n * es) &&
           editor_move(e->arena, e->tags, to * EDITOR_TAG_MAX, from * EDITOR_TAG_MAX, n * EDITOR_TAG_MAX);
}

static bool cp_remove(editor *e, size_t i) {
    if (!cp_shift(e, i, i + 1u, e->cp_count - i - 1u)) {
        return false;
    }
    e->cp_count--;
    return true;
}

static bool cp_insert(editor *e, size_t i, size_t off, size_t seen, const sqltok_state *st) {
    if (e->cp_count >= EDITOR_CHECKPOINTS || st->tag.len > EDITOR_TAG_MAX ||
        !cp_shift(e, i + 1u, i, e->cp_count - i)) {
        return false;
    }
    editor_checkpoint c = {off, seen, st->depth, st->mode, st->quote, (uint8_t)st->tag.len};
    sc_buf b = sc_buf_new(slot(e, i, EDITOR_TAG_MAX));
    if (!SC_TSLICE_SET(editor_checkpoint, e->arena, e->cps, i, &c) ||
        !sc_buf_str(e->arena, &b, st->tag_arena, st->tag)) {
        return false;
    }
    e->cp_count++;
    return true;
}

static size_t keep_before(editor *e, size_t at, size_t removed, size_t inserted) {
    size_t base = 0, w = 0;
    for (size_t i = 0; i < e->cp_count; i++) {
        editor_checkpoint c;
        if (!SC_TSLICE_GET(editor_checkpoint, e->arena, e->cps, i, &c)) {
            break;
        }
        bool before = c.seen <= at;
        if (!before && c.off < at + removed) {
            continue;
        }
        if (!before) {
            c.off = c.off - removed + inserted;
            c.seen = c.seen - removed + inserted;
        }
        if (!SC_TSLICE_SET(editor_checkpoint, e->arena, e->cps, w, &c) ||
            !editor_move(e->arena, e->tags, w * EDITOR_TAG_MAX, i * EDITOR_TAG_MAX, EDITOR_TAG_MAX)) {
            break;
        }
        base = before ? w : base;
        w++;
    }
    e->cp_count = w > 0 ? w : 1u;
    return base;
}

static size_t widen(size_t off, size_t end, size_t len) {
    size_t span = end - off;
    return span >= len - end ? len : end + span;
}

void editor_relex(editor *e, size_t at, size_t removed, size_t inserted) {
    size_t len = editor_len(e);
    size_t k = keep_before(e, at, removed, inserted);
    editor_checkpoint c;
    if (!get_cp(e, k, &c)) {
        return;
    }
    size_t off = c.off;
    sqltok_state st = load(e, k);
    for (;;) {
        editor_checkpoint nx;
        bool next = get_cp(e, k + 1u, &nx);
        size_t end = next && nx.seen > off && nx.seen - off <= 2u * EDITOR_WINDOW ? nx.seen
                                                                                : off + EDITOR_WINDOW;
        end = end < len ? end : len;
        if (!keep_tag(e, &st)) {
            e->cp_count = k + 1u;
            return;
        }
        sqltok_state s0 = st;
        size_t stop = off;
        for (;;) {
            sc_str w;
            st = s0;
            if (!window(e, off, end, &w)) {
                e->cp_count = k + 1u;
                return;
            }
            stop = off + run_window(e->scratch, w, end == len, &st);
            e->relexed += end - off;
            if (end == len || (stop > off && st.tag.len <= EDITOR_TAG_MAX)) {
                break;
            }
            end = widen(off, end, len);
        }
        while (get_cp(e, k + 1u, &nx) && nx.off <= stop) {
            if (nx.off == stop && same(e, k + 1u, &st)) {
                return;
            }
            if (!cp_remove(e, k + 1u)) {
                e->cp_count = k + 1u;
                return;
            }
        }
        if (end == len) {
            return;
        }
        k += cp_insert(e, k + 1u, stop, end, &st) ? 1u : 0u;
        off = stop;
    }
}

void editor_set_syntax(editor *e, bool std, uint8_t dialect) {
    if (std == e->std_strings && dialect == e->dialect) {
        return;
    }
    e->std_strings = std;
    e->dialect = dialect;
    e->cp_count = 1;
    e->version++;
    e->stmt_known = false;
    editor_relex(e, 0, 0, editor_len(e));
}

static bool gap_token(sqltok_token t) {
    return t.kind == SQLTOK_SPACE || t.kind == SQLTOK_LINE_COMMENT || t.kind == SQLTOK_BLOCK_COMMENT ||
           (t.kind == SQLTOK_UNTERMINATED && t.next.mode == SQLTOK_IN_COMMENT);
}

static void content(const editor *e, sc_str s, size_t from, size_t to, size_t *cs, size_t *ce) {
    sqltok_state st = {0};
    st.std_strings = e->std_strings;
    st.dialect = e->dialect;
    st.tag_arena = e->scratch;
    *cs = to;
    *ce = to;
    bool found = false;
    for (size_t k = from; k < to;) {
        sqltok_token t = sqltok_step(e->scratch, s, k, st, true);
        if (t.kind == SQLTOK_NONE) {
            break;
        }
        if (!gap_token(t)) {
            *cs = found ? *cs : k;
            *ce = t.end;
            found = true;
        }
        k = t.end;
        st = t.next;
    }
}

bool editor_statement(editor *e, size_t *from, size_t *to) {
    if (!e->stmt_known || e->stmt_version != e->version || e->stmt_cursor != e->cursor) {
        e->stmt_known = true;
        e->stmt_version = e->version;
        e->stmt_cursor = e->cursor;
        e->stmt_some = false;
        size_t n = editor_len(e);
        sc_str s = {NULL, 0, 0};
        if (n > 0 && sc_arena_reset(e->scratch)) {
            s = editor_copy(e, 0, n, e->scratch);
        }
        for (size_t start = 0; n > 0 && s.len == n;) {
            size_t semi = sqltok_stmt_end(e->scratch, s, start, e->std_strings, e->dialect);
            size_t end = semi < n ? semi + 1u : n;
            size_t cs = end, ce = end;
            content(e, s, start, end, &cs, &ce);
            if (cs < ce) {
                if (e->stmt_some && cs > e->cursor) {
                    break;
                }
                e->stmt_some = true;
                e->stmt_from = cs;
                e->stmt_to = ce;
            }
            if (end >= n) {
                break;
            }
            start = end;
        }
    }
    *from = e->stmt_from;
    *to = e->stmt_to;
    return e->stmt_some;
}

void editor_run_range(editor *e, bool all, size_t *from, size_t *to) {
    *from = 0;
    *to = all ? editor_len(e) : 0u;
    if (!all) {
        editor_selection(e, from, to);
        if (*from == *to && !editor_statement(e, from, to)) {
            *from = 0;
            *to = 0;
        }
    }
    e->sealed = true;
}

static uint8_t ink(sqltok_token t, const sqlkw *kw, const sc_arena *a, sc_str w, size_t start) {
    switch (t.kind) {
    case SQLTOK_STRING:
    case SQLTOK_DOLLAR:
        return EDITOR_INK_STRING;
    case SQLTOK_LINE_COMMENT:
    case SQLTOK_BLOCK_COMMENT:
        return EDITOR_INK_COMMENT;
    case SQLTOK_UNTERMINATED:
        return t.next.mode == SQLTOK_IN_COMMENT ? EDITOR_INK_COMMENT
               : t.next.mode == SQLTOK_IN_QUOTE && t.next.quote >= SQLTOK_Q_IDENT ? EDITOR_INK_TEXT
                                                                                 : EDITOR_INK_STRING;
    case SQLTOK_WORD:
        return kw != NULL && sqlkw_is(kw, a, sc_slice_sub(a, w, start, t.end - start)) ? EDITOR_INK_KEYWORD
                                                                                       : EDITOR_INK_TEXT;
    default:
        return EDITOR_INK_TEXT;
    }
}

static void emit(const sc_arena *a, sc_tslice out, size_t *n, editor_span s) {
    editor_span last;
    if (*n > 0 && SC_TSLICE_GET(editor_span, a, out, *n - 1u, &last) && last.ink == s.ink && last.to == s.from) {
        last.to = s.to;
        (void)SC_TSLICE_SET(editor_span, a, out, *n - 1u, &last);
        return;
    }
    if (SC_TSLICE_SET(editor_span, a, out, *n, &s)) {
        (*n)++;
    }
}

size_t editor_spans(editor *e, const sqlkw *kw, size_t from, size_t to, sc_arena *dst, sc_tslice *out) {
    size_t len = editor_len(e);
    to = to < len ? to : len;
    sc_tslice none = {{NULL, 0, dst->epoch}, 0};
    *out = none;
    if (from >= to) {
        return 0;
    }
    *out = SC_TSLICE_ALLOC(editor_span, dst, to - from);
    size_t k = 0;
    editor_checkpoint c;
    for (size_t i = 1; get_cp(e, i, &c) && c.off <= from; i++) {
        k = i;
    }
    if (sc_tslice_len(*out) != to - from || !get_cp(e, k, &c)) {
        return 0;
    }
    size_t off = c.off, n = 0;
    sqltok_state st = load(e, k);
    while (off < to) {
        if (!keep_tag(e, &st)) {
            return 0;
        }
        sqltok_state s0 = st;
        size_t end = len - off > EDITOR_WINDOW ? off + EDITOR_WINDOW : len;
        size_t n0 = n, stop = off;
        editor_span kept = {0, 0, 0};
        bool merged = n0 > 0 && SC_TSLICE_GET(editor_span, dst, *out, n0 - 1u, &kept);
        for (;;) {
            sc_str w;
            st = s0;
            n = n0;
            if ((merged && !SC_TSLICE_SET(editor_span, dst, *out, n0 - 1u, &kept)) || !window(e, off, end, &w)) {
                return 0;
            }
            size_t i = 0;
            for (;;) {
                sqltok_token t = sqltok_step(e->scratch, w, i, st, end == len);
                if (t.kind == SQLTOK_NONE) {
                    break;
                }
                if (off + t.end > from && off + i < to) {
                    editor_span s = {off + i > from ? off + i : from, off + t.end < to ? off + t.end : to,
                                     ink(t, kw, e->scratch, w, i)};
                    emit(dst, *out, &n, s);
                }
                i = t.end;
                st = t.next;
            }
            stop = off + i;
            if (end == len || (stop > off && st.tag.len <= EDITOR_TAG_MAX)) {
                break;
            }
            end = widen(off, end, len);
        }
        off = stop;
    }
    return n;
}
