#ifndef EXPLORER_SQLTOK_CHECK_H
#define EXPLORER_SQLTOK_CHECK_H

#include <assert.h>
#include <string.h>

#ifndef MAXTOK
#error "define MAXTOK before including explorer_sqltok_check.h"
#endif

typedef struct {
    size_t count;
    size_t end[MAXTOK];
    uint8_t kind[MAXTOK];
} walk;

static sqltok_state start(bool std) {
    sqltok_state st;
    memset(&st, 0, sizeof st);
    st.std_strings = std;
    return st;
}

/* Lexes `s` from `off` to its end in state `st`, with the end as end of input:
 * non-empty tokens that tile it. */
static void lex_all(const sc_arena *a, sc_str s, size_t off, sqltok_state st, walk *w) {
    while (off < s.len) {
        sqltok_token t = sqltok_step(a, s, off, st, true);
        assert(t.kind != SQLTOK_NONE && t.kind <= SQLTOK_UNTERMINATED);
        assert(t.end > off && t.end <= s.len && w->count < MAXTOK);
        w->end[w->count] = t.end;
        w->kind[w->count] = t.kind;
        w->count++;
        off = t.end;
        st = t.next;
    }
}

static bool construct(uint8_t kind) {
    return kind == SQLTOK_STRING || kind == SQLTOK_QIDENT || kind == SQLTOK_DOLLAR ||
           kind == SQLTOK_BLOCK_COMMENT;
}

/* Lexes s[0 .. cut) with more to come, then the rest from where that
 * stopped, and checks the pieces against the whole walk `w`. */
static void check_cut(const sc_arena *a, sc_str s, bool std, size_t cut, const walk *w) {
    static walk p;
    sc_str head = sc_slice_sub(a, s, 0, cut);
    size_t off = 0;
    sqltok_state st = start(std);
    p.count = 0;
    for (;;) {
        sqltok_token t = sqltok_step(a, head, off, st, false);
        if (t.kind == SQLTOK_NONE) {
            assert(t.end == off && t.next.mode == st.mode && t.next.depth == st.depth &&
                   t.next.quote == st.quote && t.next.tag.len == st.tag.len &&
                   t.next.tag.ptr == st.tag.ptr && t.next.std_strings == st.std_strings);
            break;
        }
        assert(t.end > off && t.end <= cut && p.count < MAXTOK);
        p.end[p.count] = t.end;
        p.kind[p.count] = t.kind;
        p.count++;
        off = t.end;
        st = t.next;
    }
    size_t k = 0, from = 0;
    while (w->end[k] <= off) {
        from = w->end[k];
        k++;
    }
    uint8_t b = 0;
    assert(sc_slice_get(a, s, off, &b));
    assert(construct(w->kind[k]) || w->kind[k] == SQLTOK_UNTERMINATED ||
           (from == off &&
            (w->end[k] >= cut ||
             (((w->kind[k] == SQLTOK_OP && (b == 'u' || b == 'U')) || w->kind[k] == SQLTOK_NUMBER) &&
              w->end[k] + 1 == cut) ||
             (w->kind[k] == SQLTOK_OP && b == '$'))));
    lex_all(a, s, off, st, &p);
    size_t i = 0, j = 0;
    while (i < w->count) {
        assert(j < p.count);
        if (p.end[j] == w->end[i]) {
            assert(p.kind[j] == w->kind[i]);
            i++;
            j++;
            continue;
        }
        assert(p.end[j] == off && p.end[j] < w->end[i]);
        assert(p.kind[j] == w->kind[i] ||
               (w->kind[i] == SQLTOK_UNTERMINATED && construct(p.kind[j])));
        assert(construct(w->kind[i]) || w->kind[i] == SQLTOK_UNTERMINATED);
        j++;
    }
    assert(j == p.count);
}

#endif
