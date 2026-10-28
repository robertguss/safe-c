/* Fuzzes the explorer's SQL editor (examples/explorer/editor.c, plan U22):
 * random edits, selections, undo and redo against a flat-buffer reference
 * with its own snapshot history, after every step; and the checkpointed
 * highlighting of a random range against a lex of the whole text, with
 * windows small enough that every edit meets checkpoints. */
#define EDITOR_WINDOW 8u
#include "../examples/explorer/editor.c"
#include "../examples/explorer/editor_syntax.c"
#include "../examples/explorer/frame.c"
#include "../examples/explorer/sqlkw.c"
#include "../examples/explorer/sqltok.c"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

#define MAXT 16384u
#define HIST (EDITOR_UNDO_STEPS + 1u)

static alignas(8) uint8_t ed_mem[EDITOR_BYTES];
static alignas(8) uint8_t scratch_mem[EDITOR_SCRATCH_BYTES];
static alignas(8) uint8_t ev_mem[MAXT + 64];
static alignas(8) uint8_t out_mem[MAXT * 32u + 64];
static alignas(8) uint8_t kw_mem[16384];
static alignas(8) uint8_t flat_mem[MAXT + 64];

static const char *const vocab[] = {
    "select", " ", "\n", "'", "$f$", "$", "--", "/*", "*/", ";", "\xC3\xA9", "\xF0\x9F\x98\x80", "x", "\"",
    "E'", "\\", "1e5", "\r\n", "begin", "end", "(", ")", "$1", "case", "create function", "u&'", "\t", "$g$",
};

typedef struct {
    uint8_t text[MAXT];
    size_t len, cursor, anchor, after, bc, ba;
} state_snap;

static state_snap hist[HIST];
static size_t first, count, done;
static state_snap cur;
static uint8_t last_kind;
static uint64_t last_ms;
static bool sealed = true;

static size_t lo(void) {
    return cur.cursor < cur.anchor ? cur.cursor : cur.anchor;
}

static size_t hi(void) {
    return cur.cursor < cur.anchor ? cur.anchor : cur.cursor;
}

static state_snap *at_hist(size_t k) {
    return &hist[(first + k) % HIST];
}

static void ref_replace(size_t from, size_t to, const uint8_t *s, size_t n, uint8_t kind, uint64_t ms) {
    bool joins = !sealed && kind != STEP_OTHER && kind == last_kind && done == count && done > 0 &&
                 ms - last_ms < EDITOR_GROUP_MS &&
                 (kind == STEP_TYPE   ? from == to && from == cur.after
                  : kind == STEP_BACK ? n == 0 && to == cur.after
                                      : n == 0 && from == cur.after);
    size_t bc = cur.cursor, ba = cur.anchor;
    if (!joins) {
        count = done;
        if (count == EDITOR_UNDO_STEPS) {
            first = (first + 1u) % HIST;
            count--;
            done--;
        }
        done++;
        count++;
    }
    memmove(cur.text + from + n, cur.text + to, cur.len - to);
    memcpy(cur.text + from, s, n);
    cur.len = cur.len - (to - from) + n;
    cur.cursor = cur.anchor = from + n;
    cur.after = from + n;
    state_snap *h = at_hist(done);
    size_t keep_bc = h->bc, keep_ba = h->ba;
    *h = cur;
    h->bc = joins ? keep_bc : bc;
    h->ba = joins ? keep_ba : ba;
    last_kind = kind;
    last_ms = ms;
    sealed = kind == STEP_OTHER;
}

static size_t ref_snap(size_t p) {
    while (p > 0 && p < cur.len && (cur.text[p] & 0xC0u) == 0x80u) {
        p--;
    }
    return p;
}

static size_t ref_prev(size_t p) {
    if (p == 0) {
        return 0;
    }
    p--;
    while (p > 0 && (cur.text[p] & 0xC0u) == 0x80u) {
        p--;
    }
    return p;
}

static size_t ref_next(size_t p) {
    if (p >= cur.len) {
        return cur.len;
    }
    p++;
    while (p < cur.len && (cur.text[p] & 0xC0u) == 0x80u) {
        p++;
    }
    return p;
}

static void agree(editor *e) {
    assert(editor_len(e) == cur.len && e->cursor == cur.cursor && e->anchor == cur.anchor);
    for (size_t i = 0; i < cur.len; i++) {
        uint8_t c = 0;
        assert(editor_byte(e, i, &c) && c == cur.text[i]);
    }
    size_t lines = 1;
    for (size_t i = 0; i < cur.len; i++) {
        lines += cur.text[i] == '\n' ? 1u : 0u;
    }
    assert(e->lines == lines);
}

static void check_inks(editor *e, const sqlkw *kw, sc_arena *out, sc_arena *flat, size_t from, size_t to) {
    static uint8_t want[MAXT];
    assert(sc_arena_reset(flat));
    sc_str s = sc_str_from_lit(flat, cur.text, cur.len);
    sqltok_state st = {0};
    st.std_strings = true;
    st.tag_arena = flat;
    for (size_t k = 0; k < cur.len;) {
        sqltok_token t = sqltok_step(flat, s, k, st, true);
        assert(t.kind != SQLTOK_NONE);
        uint8_t i = ink(t, kw, flat, s, k);
        memset(want + k, i, t.end - k);
        k = t.end;
        st = t.next;
    }
    sc_tslice sp;
    assert(sc_arena_reset(out));
    size_t n = editor_spans(e, kw, from, to, out, &sp);
    size_t pos = from;
    for (size_t i = 0; i < n; i++) {
        editor_span x;
        assert(SC_TSLICE_GET(editor_span, out, sp, i, &x) && x.from == pos && x.to > x.from);
        for (size_t k = x.from; k < x.to; k++) {
            assert(want[k] == x.ink);
        }
        pos = x.to;
    }
    assert(pos == (to < cur.len ? to : cur.len) || from >= cur.len);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static sc_arena ed_a, scratch_a, ev_a, out_a, kw_a, flat_a;
    static editor e;
    static sqlkw kw;
    static bool ready;
    static uint8_t frag[MAXT];
    if (!ready) {
        sc_arena_init(&kw_a, kw_mem, sizeof kw_mem);
        assert(sqlkw_init(&kw, &kw_a));
        ready = true;
    }
    sc_arena_init(&ed_a, ed_mem, sizeof ed_mem);
    sc_arena_init(&scratch_a, scratch_mem, sizeof scratch_mem);
    sc_arena_init(&ev_a, ev_mem, sizeof ev_mem);
    sc_arena_init(&out_a, out_mem, sizeof out_mem);
    sc_arena_init(&flat_a, flat_mem, sizeof flat_mem);
    assert(editor_init(&e, &ed_a, &scratch_a, true));
    memset(&cur, 0, sizeof cur);
    first = count = done = 0;
    *at_hist(0) = cur;
    sealed = true;
    uint64_t ms = 1000;
    for (size_t i = 0; i + 2 < size; i += 3) {
        uint8_t op = data[i] % 9u, a = data[i + 1], b = data[i + 2];
        ms += (a & 1u) ? 600u : 100u;
        if (op <= 2) {
            const char *v = vocab[a % (sizeof vocab / sizeof vocab[0])];
            size_t vn = strlen(v), reps = op == 2 ? 1u + b % 200u : 1u, n = 0, keep = 0;
            for (size_t r = 0; r < reps && n + vn <= sizeof frag; r++) {
                memcpy(frag + n, v, vn);
                n += vn;
            }
            for (size_t k = 0; k < n; k++) {
                keep += frag[k] == '\r' ? 0u : 1u;
            }
            assert(sc_arena_reset(&ev_a));
            editor_status st = editor_insert(&e, &ev_a, sc_str_from_lit(&ev_a, frag, n), op == 0, ms);
            if (cur.len - (hi() - lo()) + keep > MAXT) {
                if (st == EDITOR_OK) {
                    return 0;
                }
                continue;
            }
            assert(st == EDITOR_OK);
            size_t w = 0;
            for (size_t k = 0; k < n; k++) {
                if (frag[k] != '\r') {
                    frag[w++] = frag[k];
                }
            }
            if (w > 0 || hi() > lo()) {
                ref_replace(lo(), hi(), frag, w, op == 0 ? STEP_TYPE : STEP_OTHER, ms);
            }
        } else if (op == 3) {
            size_t an = ref_snap(cur.len ? a * cur.len / 255u : 0), cu = ref_snap(cur.len ? b * cur.len / 255u : 0);
            editor_select(&e, an, cu);
            cur.anchor = an;
            cur.cursor = cu;
            sealed = true;
        } else if (op == 4 || op == 5) {
            bool fwd = op == 5;
            editor_press p = {fwd ? EDITOR_CMD_DELETE : EDITOR_CMD_BACKSPACE, false};
            (void)editor_do(&e, p, ms);
            size_t from = lo(), to = hi();
            uint8_t kind = STEP_OTHER;
            if (from == to) {
                from = fwd ? cur.cursor : ref_prev(cur.cursor);
                to = fwd ? ref_next(cur.cursor) : cur.cursor;
                kind = fwd ? STEP_DEL : STEP_BACK;
            }
            if (from < to) {
                ref_replace(from, to, frag, 0, kind, ms);
            }
        } else if (op == 6) {
            editor_press p = {EDITOR_CMD_UNDO, false};
            bool did = editor_do(&e, p, ms);
            assert(did == (done > 0));
            if (did) {
                size_t c = at_hist(done)->bc, an = at_hist(done)->ba;
                done--;
                cur = *at_hist(done);
                cur.cursor = c;
                cur.anchor = an;
                sealed = true;
            }
        } else if (op == 8) {
            static const editor_cmd line_cmds[] = {EDITOR_CMD_COMMENT, EDITOR_CMD_INDENT, EDITOR_CMD_OUTDENT};
            editor_press p = {line_cmds[a % 3u], false};
            size_t from = lo(), last = hi();
            while (from > 0 && cur.text[from - 1u] != '\n') {
                from--;
            }
            last = hi() > lo() && last > from && cur.text[last - 1u] == '\n' ? last - 1u : last;
            size_t to = last;
            while (to < cur.len && cur.text[to] != '\n') {
                to++;
            }
            if (editor_do(&e, p, ms)) {
                if (editor_len(&e) > MAXT) {
                    return 0;
                }
                size_t n = editor_len(&e) + (to - from) - cur.len;
                for (size_t k = 0; k < n; k++) {
                    assert(editor_byte(&e, from + k, &frag[k]));
                }
                ref_replace(from, to, frag, n, STEP_OTHER, ms);
                cur.cursor = e.cursor;
                cur.anchor = e.anchor;
            }
        } else {
            editor_press p = {EDITOR_CMD_REDO, false};
            bool did = editor_do(&e, p, ms);
            assert(did == (done < count));
            if (did) {
                done++;
                cur = *at_hist(done);
                cur.anchor = cur.cursor = cur.after;
                sealed = true;
            }
        }
        agree(&e);
        if (cur.len > 0) {
            size_t f = a * cur.len / 256u, t = f + 1u + b * 16u;
            check_inks(&e, &kw, &out_a, &flat_a, f, t);
        }
    }
    return 0;
}
