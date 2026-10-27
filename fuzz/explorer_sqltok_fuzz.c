/* Fuzzes the explorer's SQL tokenizer and statement splitter
 * (examples/explorer/sqltok.c, plan U14). Byte 0 picks standard_conforming_strings
 * and whether the rest is raw bytes or picks from a SQL vocabulary; byte 1
 * picks a cut. Checks: the tokens tile the input; lexing it in two windows
 * cut anywhere gives the same tokens with only an open construct split; and
 * the split offsets match a reference splitter written here from psql's
 * lexer rules (src/fe_utils/psqlscan.l) a different way: it walks bytes
 * with flex's longest match per rule, where sqltok.c lexes tokens. */
#include "../examples/explorer/sqltok.c"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

#define MAXN 1024u
#define MAXTOK (MAXN + 1u)

static const char *const vocab[] = {
    "create ", "CREATE ", "or ", "replace ", "function ", "procedure ", "begin ", "case ",
    "end ", "atomic ", ";", "(", ")", "'", "\"", "$", "$a$", "$$", "$1", "E'", "e'", "U&'",
    "u&\"", "u&", "B'", "X'", "N'", "\\", "--", "/*", "*/", "\n", "\r", " ", "\t", "1", "0x",
    "e", "_", ".", "-", "+", "x", "a", "''", "\\'", "--x\n", "1e-", "0", "$a", "*", "/",
    "'\n'", "\f", "begin", "end", "1_.", "Create", "Function", "q",
};

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

/* (a) Lexes `s` from `off` to its end as end of input: non-empty tokens
 * that tile it. */
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

/* (b) Lexes s[0 .. cut) with more to come, then the rest, and checks the
 * pieces against the whole walk `w`, as the unit test's check_cut does. */
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
           (from == off && (w->end[k] + 1 >= cut || (w->kind[k] == SQLTOK_OP && b == '$'))));
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

/* ---- The reference splitter. `at` is the byte at k, or -1 past the end. ---- */
static const uint8_t *rp;
static size_t rn;
static bool r_open;
static size_t rcount;
static size_t rend[MAXTOK];
static uint8_t rkind[MAXTOK];

static void r_emit(size_t end, uint8_t kind) {
    if (kind == SQLTOK_SPACE && rcount > 0 && rkind[rcount - 1] == SQLTOK_SPACE) {
        rend[rcount - 1] = end;
        return;
    }
    assert(rcount < MAXTOK);
    rend[rcount] = end;
    rkind[rcount] = kind;
    rcount++;
}

static void r_construct(size_t end, uint8_t kind) {
    r_emit(end, r_open ? SQLTOK_UNTERMINATED : kind);
    r_open = false;
}

static int at(size_t k) {
    return k < rn ? rp[k] : -1;
}

static bool r_digit(int c) {
    return c >= '0' && c <= '9';
}

static bool r_alpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

static bool r_cont(int c) {
    return r_alpha(c) || r_digit(c) || c == '$';
}

static bool r_space(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static bool r_newline(int c) {
    return c == '\n' || c == '\r';
}

/* The end of an identifier run from k, which starts one. */
static size_t r_ident_end(size_t k) {
    k++;
    while (r_cont(at(k))) {
        k++;
    }
    return k;
}

/* Marks every end of {decinteger} = digit (_? digit)* from s. */
static void mark_dec(size_t s, bool *ends) {
    if (!r_digit(at(s))) {
        return;
    }
    size_t k = s + 1;
    ends[k] = true;
    for (;;) {
        if (r_digit(at(k))) {
            k++;
        } else if (at(k) == '_' && r_digit(at(k + 1))) {
            k += 2;
        } else {
            return;
        }
        ends[k] = true;
    }
}

/* The token end of the number rules' longest match at i (one of them
 * matches). {numericfail} keeps only its integer (yyless). */
static size_t r_number(size_t i) {
    static bool dec[MAXN + 4], num[MAXN + 4], real[MAXN + 4];
    memset(dec, 0, sizeof dec);
    memset(num, 0, sizeof num);
    memset(real, 0, sizeof real);
    size_t best = i, tok_end = i;
#define CAND(e)                \
    do {                       \
        if ((e) > best) {      \
            best = (e);        \
            tok_end = (e);     \
        }                      \
    } while (0)
    mark_dec(i, dec);
    for (size_t e = i; e <= rn; e++) {
        if (dec[e] && at(e) == '.') {
            num[e + 1] = true;
            mark_dec(e + 1, num);
            if (at(e + 1) == '.' && e + 2 > best) {
                best = e + 2;
                tok_end = e;
            }
        }
    }
    if (at(i) == '.') {
        mark_dec(i + 1, num);
    }
    for (size_t e = i; e <= rn; e++) {
        if ((dec[e] || num[e]) && (at(e) == 'e' || at(e) == 'E')) {
            size_t s = e + 1;
            if (at(s) == '+' || at(s) == '-') {
                CAND(e + 2); /* {realfail} */
                s++;
            }
            mark_dec(s, real);
        }
    }
    for (size_t e = i; e <= rn; e++) {
        if (dec[e] || num[e] || real[e]) {
            CAND(e);
            if (r_alpha(at(e))) {
                CAND(r_ident_end(e)); /* the _junk rules */
            }
        }
    }
    int x = at(i + 1);
    if (at(i) == '0' && (x == 'x' || x == 'X' || x == 'o' || x == 'O' || x == 'b' || x == 'B')) {
        size_t k = i + 2;
        CAND(k); /* {hexfail} and friends */
        if (at(k) == '_') {
            CAND(k + 1);
        }
        for (;;) {
            int d = at(k) == '_' ? at(k + 1) : at(k);
            int lo = d | 0x20;
            bool ok = (x | 0x20) == 'x'   ? r_digit(d) || (lo >= 'a' && lo <= 'f')
                      : (x | 0x20) == 'o' ? d >= '0' && d <= '7'
                                          : d == '0' || d == '1';
            if (!ok) {
                break;
            }
            k += at(k) == '_' ? 2u : 1u;
            CAND(k);
        }
    }
#undef CAND
    return tok_end;
}

/* {quotecontinue} after a closing quote at j - 1: whitespace with a newline
 * (line comments allowed), then a quote. Its offset, or 0. */
static size_t r_continue(size_t k) {
    for (;;) {
        int c = at(k);
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
            k++;
        } else if (c == '-' && at(k + 1) == '-') {
            k += 2;
            while (k < rn && !r_newline(rp[k])) {
                k++;
            }
        } else {
            break;
        }
    }
    if (!r_newline(at(k))) {
        return 0;
    }
    k++;
    for (;;) {
        if (r_space(at(k))) {
            k++;
        } else if (at(k) == '-' && at(k + 1) == '-') {
            size_t m = k + 2;
            while (m < rn && !r_newline(rp[m])) {
                m++;
            }
            if (m == rn) {
                break;
            }
            k = m + 1;
        } else {
            break;
        }
    }
    return at(k) == '\'' ? k : 0;
}

#define R_Q 0  /* <xq>, <xus>: '' doubles */
#define R_E 1  /* <xe>: backslash takes the next byte too */
#define R_BX 2 /* <xb>, <xh>: no doubling */

/* A string body from j; the offset after it, or rn when it stays open. */
static size_t r_string(size_t j, int kind) {
    while (j < rn) {
        int c = rp[j];
        if (kind == R_E && c == '\\') {
            j += j + 1 < rn ? 2u : 1u;
            continue;
        }
        if (c == '\'') {
            if (kind != R_BX && at(j + 1) == '\'') {
                j += 2;
                continue;
            }
            size_t q = r_continue(j + 1);
            if (q == 0) {
                return j + 1;
            }
            j = q + 1;
            continue;
        }
        j++;
    }
    r_open = true;
    return rn;
}

static size_t r_dquote(size_t j) {
    while (j < rn) {
        if (rp[j] == '"') {
            if (at(j + 1) != '"') {
                return j + 1;
            }
            j++;
        }
        j++;
    }
    r_open = true;
    return rn;
}

/* A block comment body from j (after the first slash-star). */
static size_t r_comment(size_t j) {
    size_t depth = 0;
    while (j < rn) {
        if (rp[j] == '/' && at(j + 1) == '*') {
            depth++;
            j += 2;
        } else if (rp[j] == '*') {
            size_t k = j;
            while (at(k) == '*') {
                k++;
            }
            if (at(k) == '/') {
                if (depth == 0) {
                    return k + 1;
                }
                depth--;
                k++;
            }
            j = k;
        } else {
            j++;
        }
    }
    r_open = true;
    return rn;
}

/* A dollar body from j, closed by the delimiter rp[t .. t + tn). */
static size_t r_dollar(size_t j, size_t t, size_t tn) {
    while (j < rn) {
        if (rp[j] != '$') {
            j++;
            continue;
        }
        size_t m = j + 1;
        if (r_alpha(at(m))) {
            m++;
            while (r_alpha(at(m)) || r_digit(at(m))) {
                m++;
            }
        }
        if (at(m) == '$') {
            if (m + 1 - j == tn && memcmp(rp + j, rp + t, tn) == 0) {
                return m + 1;
            }
            j = m; /* yyless(yyleng - 1): the last $ may open the real end */
        } else {
            j = m;
        }
    }
    r_open = true;
    return rn;
}

static bool r_word(size_t i, size_t k, const char *w) {
    size_t n = strlen(w);
    if (k - i != n) {
        return false;
    }
    for (size_t m = 0; m < n; m++) {
        if ((rp[i + m] | 0x20) != w[m]) {
            return false;
        }
    }
    return true;
}

typedef struct {
    size_t paren, begin, count;
    char idents[4];
} r_state;

/* psqlscan_track_identifier: the first four identifiers outside parentheses,
 * and BEGIN/CASE/END depth once they read CREATE [OR REPLACE] FUNCTION or
 * PROCEDURE. */
static void r_track(r_state *r, size_t i, size_t k) {
    static const char *const keys[] = {"create", "function", "procedure", "or", "replace"};
    if (r->paren != 0) {
        return;
    }
    if (r->count == 0) {
        memset(r->idents, 0, sizeof r->idents);
    }
    if (r->count < 4) {
        for (size_t m = 0; m < 5; m++) {
            if (r_word(i, k, keys[m])) {
                r->idents[r->count] = keys[m][0];
            }
        }
        r->count++;
    }
    const char *d = r->idents;
    if (!(d[0] == 'c' &&
          (d[1] == 'f' || d[1] == 'p' || (d[1] == 'o' && d[2] == 'r' && (d[3] == 'f' || d[3] == 'p'))))) {
        return;
    }
    if (r_word(i, k, "begin")) {
        r->begin++;
    } else if (r_word(i, k, "case") && r->begin >= 1) {
        r->begin++;
    } else if (r_word(i, k, "end") && r->begin > 0) {
        r->begin--;
    }
}

/* Every split offset of p[0 .. n), into out, and its tokens into rend and rkind. */
static size_t ref_splits(const uint8_t *p, size_t n, bool std, size_t *out) {
    r_state r;
    memset(&r, 0, sizeof r);
    size_t count = 0, i = 0;
    rp = p;
    rn = n;
    rcount = 0;
    r_open = false;
    while (i < n) {
        int c = p[i], d = at(i + 1);
        int l = c | 0x20;
        if (r_space(c)) {
            i++;
            r_emit(i, SQLTOK_SPACE);
        } else if (c == '-' && d == '-') {
            while (i < n && !r_newline(p[i])) {
                i++;
            }
            r_emit(i, SQLTOK_LINE_COMMENT);
        } else if (c == '/' && d == '*') {
            i = r_comment(i + 2);
            r_construct(i, SQLTOK_BLOCK_COMMENT);
        } else if (c == '\'') {
            i = r_string(i + 1, std ? R_Q : R_E);
            r_construct(i, SQLTOK_STRING);
        } else if (c == '"') {
            i = r_dquote(i + 1);
            r_construct(i, SQLTOK_QIDENT);
        } else if (l == 'e' && d == '\'') {
            i = r_string(i + 2, R_E);
            r_construct(i, SQLTOK_STRING);
        } else if ((l == 'b' || l == 'x') && d == '\'') {
            i = r_string(i + 2, R_BX);
            r_construct(i, SQLTOK_STRING);
        } else if (l == 'n' && d == '\'') {
            i++; /* {xnstart}: yyless(1); the string that follows is the token */
        } else if (l == 'u' && d == '&') {
            int e = at(i + 2);
            if (e == '\'') {
                i = r_string(i + 3, R_Q);
                r_construct(i, SQLTOK_STRING);
            } else if (e == '"') {
                i = r_dquote(i + 3);
                r_construct(i, SQLTOK_QIDENT);
            } else {
                i++;
                r_emit(i, SQLTOK_OP);
            }
        } else if (r_alpha(c)) {
            size_t k = r_ident_end(i);
            r_track(&r, i, k);
            i = k;
            r_emit(i, SQLTOK_WORD);
        } else if (r_digit(c) || (c == '.' && r_digit(d))) {
            i = r_number(i);
            r_emit(i, SQLTOK_NUMBER);
        } else if (c == '$' && r_digit(d)) {
            size_t k = i + 1;
            while (r_digit(at(k))) {
                k++;
            }
            i = r_alpha(at(k)) ? r_ident_end(k) : k;
            r_emit(i, SQLTOK_PARAM);
        } else if (c == '$') {
            size_t k = i + 1;
            if (r_alpha(at(k))) {
                k++;
                while (r_alpha(at(k)) || r_digit(at(k))) {
                    k++;
                }
            }
            if (at(k) == '$') {
                i = r_dollar(k + 1, i, k + 1 - i);
                r_construct(i, SQLTOK_DOLLAR);
            } else {
                i++;
                r_emit(i, SQLTOK_OP);
            }
        } else {
            if (c == '(') {
                r.paren++;
            } else if (c == ')' && r.paren > 0) {
                r.paren--;
            } else if (c == ';' && r.paren == 0 && r.begin == 0) {
                out[count++] = i;
                r.count = 0;
            }
            i++;
            r_emit(i, SQLTOK_OP);
        }
    }
    return count;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static uint8_t text[MAXN];
    static size_t want[MAXN];
    static walk w;
    alignas(8) uint8_t store[2 * MAXN + 64];
    if (size < 3) {
        return 0;
    }
    bool std = (data[0] & 1u) != 0;
    size_t n = 0;
    if (data[0] & 2u) {
        for (size_t k = 2; k < size; k++) {
            const char *v = vocab[data[k] % (sizeof vocab / sizeof vocab[0])];
            size_t m = strlen(v);
            if (n + m > MAXN) {
                break;
            }
            memcpy(text + n, v, m);
            n += m;
        }
    } else {
        n = size - 2 < MAXN ? size - 2 : MAXN;
        memcpy(text, data + 2, n);
    }
    if (n == 0) {
        return 0;
    }
    memset(store, '\'', sizeof store); /* a read past the input sees a quote */
    sc_arena a;
    sc_arena_init(&a, store, sizeof store);
    sc_str s = sc_str_from_lit(&a, text, n);
    assert(s.len == n);

    w.count = 0;
    lex_all(&a, s, 0, start(std), &w);
    if (n > 1) {
        check_cut(&a, s, std, 1 + data[1] % (n - 1), &w);
    }

    size_t nwant = ref_splits(text, n, std, want);
    assert(rcount == w.count);
    for (size_t k = 0; k < rcount; k++) {
        assert(rend[k] == w.end[k] && rkind[k] == w.kind[k]);
    }
    size_t from = 0, got = 0;
    for (;;) {
        size_t e = sqltok_stmt_end(&a, s, from, std);
        assert(e >= from && e <= n);
        if (e == n) {
            break;
        }
        assert(got < nwant && want[got] == e);
        got++;
        from = e + 1;
    }
    assert(got == nwant);
    return 0;
}
