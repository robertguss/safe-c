/* Tests the explorer's SQL tokenizer and statement splitter
 * (examples/explorer/sqltok.c, plan U14). The table below is the reference:
 * each row's tokens and split offsets come from psql's lexer rules
 * (src/fe_utils/psqlscan.l), worked out by hand. Every row is also lexed in
 * two windows at every cut, as the editor's checkpoints do, and must give the
 * same tokens with only an open string, identifier, dollar string or comment
 * cut in two. Core code, built in. */
#include "../examples/explorer/sqltok.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static alignas(8) uint8_t mem[1u << 16];
static sc_arena ar;

typedef struct {
    const char *sql;
    bool std; /* standard_conforming_strings */
    const char *toks;
    const char *splits;
} row;

/* Token kinds as letters: space, word, quoted identifier, String, dollar,
 * number, parameter, line comment, block comment, operator, unterminated. */
static const row rows[] = {
    {"select 'a;b'; select 2", true, "w6 s1 S5 o1 s1 w6 s1 n1", "12"},
    {"E'it\\'s;'", true, "S9", ""},
    {"'it\\'s'", false, "S7", ""},
    {"'it\\'s'", true, "S5 w1 u1", ""},
    {"$fn$ a; b $fn$", true, "d14", ""},
    {"$1", true, "p2", ""},
    {"foo$bar$", true, "w8", ""},
    {"/* a /* b */ ; */ select 1", true, "b17 s1 w6 s1 n1", ""},
    {"\"a\"\"b;\"", true, "q7", ""},
    {"CREATE FUNCTION f() RETURNS int LANGUAGE sql BEGIN ATOMIC SELECT 1; SELECT 2; END; "
     "SELECT 3",
     true,
     "w6 s1 w8 s1 w1 o1 o1 s1 w7 s1 w3 s1 w8 s1 w3 s1 w5 s1 w6 s1 w6 s1 n1 o1 s1 w6 s1 n1 o1 "
     "s1 w3 o1 s1 w6 s1 n1",
     "81"},
    {"select (1;2)", true, "w6 s1 o1 n1 o1 n1 o1", ""},
    {"'abc", true, "u4", ""},
    {"U&'a''b'", true, "S8", ""},
    {"u&\"x\"", true, "q5", ""},
    {"u&x", true, "o1 o1 w1", ""},
    {"U&", true, "o1 o1", ""},
    {"U", true, "w1", ""},
    {"B'01'", true, "S5", ""},
    {"x'1F'", true, "S5", ""},
    {"N'x'", true, "S4", ""},
    {"b'a''b'", true, "S4 S3", ""},
    {"e'\\''", true, "S5", ""},
    {"'\\''", false, "S4", ""},
    {"'\\''", true, "u4", ""},
    {"''''", true, "S4", ""},
    {"E'a'\n'\\';'", true, "S10", ""},
    {"U&'a' --c\n'\\';'", false, "S13 o1 u1", "13"},
    {"B'1'\n'1''x'", true, "S8 S3", ""},
    {"'a' 'b'", true, "S3 s1 S3", ""},
    {"'a'\n-'b'", true, "S3 s1 o1 S3", ""},
    {"'a'\n", true, "S3 s1", ""},
    {"''", true, "S2", ""},
    {"E'\\", true, "u3", ""},
    {"1.5e+3x", true, "n7", ""},
    {"1..2", true, "n1 o1 n2", ""},
    {"1e+", true, "n3", ""},
    {"1e+x", true, "n3 w1", ""},
    {"0x1F", true, "n4", ""},
    {"1a$b$;", true, "n5 o1", "5"},
    {".5", true, "n2", ""},
    {".", true, "o1", ""},
    {"1_000", true, "n5", ""},
    {"1_$$;$$", true, "n4 o1 u2", "4"},
    {"1_.e'\\';'", true, "n2 o1 S6", ""},
    {"1_1.5_5$$;$$", true, "n9 o1 u2", "9"},
    {"1e5$$;$$", true, "n5 o1 u2", "5"},
    {"1.e5$$;$$", true, "n6 o1 u2", "6"},
    {"$1_2$$;$$", true, "p6 o1 u2", "6"},
    {"1_1.5;", true, "n5 o1", "5"},
    {"1e+5$", true, "n4 o1", ""},
    {"a.b", true, "w1 o1 w1", ""},
    {"1.x", true, "n3", ""},
    {"1$", true, "n1 o1", ""},
    {"$a$x$a$", true, "d7", ""},
    {"$a$ $b$ $a$", true, "d11", ""},
    {"$$x$$", true, "d5", ""},
    {"$a", true, "o1 w1", ""},
    {"$ab$c", true, "u5", ""},
    {"$a$$b$a$", true, "d8", ""},
    {"$1abc", true, "p5", ""},
    {"$_x$ $_x$", true, "d9", ""},
    {"$", true, "o1", ""},
    {"x$", true, "w2", ""},
    {"--x\ny", true, "l3 s1 w1", ""},
    {"--x\r", true, "l3 s1", ""},
    {"-", true, "o1", ""},
    {"a-b", true, "w1 o1 w1", ""},
    {"/*/ */", true, "b6", ""},
    {"/*", true, "u2", ""},
    {"a/*x*/b", true, "w1 b5 w1", ""},
    {"/* /* */", true, "u8", ""},
    {"/***/", true, "b5", ""},
    {"*/", true, "o1 o1", ""},
    {"\"abc", true, "u4", ""},
    {"\"\"", true, "q2", ""},
    {"\xc3\xbc", true, "w2", ""},
    {"\t\n\r\f\v ", true, "s6", ""},
    {"select 1; select 2;", true, "w6 s1 n1 o1 s1 w6 s1 n1 o1", "8 18"},
    {"create or replace procedure p() begin atomic select 1; end; select 2", true, NULL, "58"},
    {"create table t (a int); select 1", true, NULL, "22"},
    {"create function f() returns int as $$ begin return 1; end $$ language plpgsql; select 1",
     true, NULL, "77"},
    {"create function f() returns int language sql begin atomic select case when true then 1 "
     "end; end; select 2",
     true, NULL, "95"},
    {"begin; select 1; commit;", true, NULL, "5 15 23"},
    {"create or function begin; x", true, NULL, "24"},
    {"CREATE PROCEDURE p() BEGIN ATOMIC SELECT 1; END; SELECT 2", true, NULL, "47"},
    {"CREATE FUNCTION f() RETURNS int LANGUAGE sql BEGIN ATOMIC SELECT CASE WHEN true THEN 1 END; "
     "SELECT 2; END; SELECT 3",
     true, NULL, "105"},
    {"create (function) begin; select 1", true, NULL, "23"},
    {"1 create function begin; end; x", true, NULL, "28"},
    {"create (x) function begin; end;", true, NULL, "30"},
    {"select 'a\\';b'; select 2", false, NULL, "14"},
    {"select 'a\\';b'; select 2", true, NULL, "11"},
};

static sc_str text(const char *t) {
    size_t n = strlen(t);
    if (n == 0) {
        return (sc_str){NULL, 0, ar.epoch};
    }
    sc_str s = sc_str_from_lit(&ar, (const uint8_t *)t, n);
    assert(s.len == n);
    return s;
}

static char letter(uint8_t kind) {
    switch (kind) {
    case SQLTOK_SPACE: return 's';
    case SQLTOK_WORD: return 'w';
    case SQLTOK_QIDENT: return 'q';
    case SQLTOK_STRING: return 'S';
    case SQLTOK_DOLLAR: return 'd';
    case SQLTOK_NUMBER: return 'n';
    case SQLTOK_PARAM: return 'p';
    case SQLTOK_LINE_COMMENT: return 'l';
    case SQLTOK_BLOCK_COMMENT: return 'b';
    case SQLTOK_OP: return 'o';
    case SQLTOK_UNTERMINATED: return 'u';
    default: return '?';
    }
}

static sqltok_state start(bool std) {
    sqltok_state st;
    memset(&st, 0, sizeof st);
    st.std_strings = std;
    return st;
}

#define MAXTOK 128u

typedef struct {
    size_t count;
    size_t end[MAXTOK];
    uint8_t kind[MAXTOK];
} walk;

/* Lexes `s` from `off` to its end in state `st`, with the end as end of input. */
static void lex_all(sc_str s, size_t off, sqltok_state st, walk *w) {
    while (off < s.len) {
        sqltok_token t = sqltok_step(&ar, s, off, st, true);
        assert(t.kind != SQLTOK_NONE && t.end > off && t.end <= s.len);
        assert(w->count < MAXTOK);
        w->end[w->count] = t.end;
        w->kind[w->count] = t.kind;
        w->count++;
        off = t.end;
        st = t.next;
    }
}

static void encode(const walk *w, char *out, size_t cap) {
    size_t at = 0, from = 0;
    out[0] = '\0';
    for (size_t i = 0; i < w->count; i++) {
        at += (size_t)snprintf(out + at, cap - at, "%s%c%zu", i ? " " : "", letter(w->kind[i]),
                               w->end[i] - from);
        from = w->end[i];
    }
}

static void splits(sc_str s, bool std, char *out, size_t cap) {
    size_t at = 0, from = 0;
    out[0] = '\0';
    for (;;) {
        size_t e = sqltok_stmt_end(&ar, s, from, std);
        assert(e <= s.len && e >= from);
        if (e == s.len) {
            return;
        }
        uint8_t b = 0;
        assert(sc_slice_get(&ar, s, e, &b) && b == ';');
        at += (size_t)snprintf(out + at, cap - at, "%s%zu", at ? " " : "", e);
        from = e + 1;
    }
}

static bool construct(uint8_t kind) {
    return kind == SQLTOK_STRING || kind == SQLTOK_QIDENT || kind == SQLTOK_DOLLAR ||
           kind == SQLTOK_BLOCK_COMMENT;
}

/* Lexes s[0 .. cut) with more to come, then the rest from where that
 * stopped, and checks the pieces against the whole walk `w`. */
static void check_cut(sc_str s, bool std, size_t cut, const walk *w) {
    sc_str head = sc_slice_sub(&ar, s, 0, cut);
    walk p = {0};
    size_t off = 0;
    sqltok_state st = start(std);
    for (;;) {
        sqltok_token t = sqltok_step(&ar, head, off, st, false);
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
    lex_all(s, off, st, &p);
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

static void table(void) {
    char got[512];
    for (size_t r = 0; r < sizeof rows / sizeof rows[0]; r++) {
        assert(sc_arena_reset(&ar));
        sc_str s = text(rows[r].sql);
        walk w = {0};
        lex_all(s, 0, start(rows[r].std), &w);
        if (rows[r].toks) {
            encode(&w, got, sizeof got);
            if (strcmp(got, rows[r].toks) != 0) {
                printf("row %zu %s: tokens %s, want %s\n", r, rows[r].sql, got, rows[r].toks);
                abort();
            }
        }
        splits(s, rows[r].std, got, sizeof got);
        if (strcmp(got, rows[r].splits) != 0) {
            printf("row %zu %s: splits %s, want %s\n", r, rows[r].sql, got, rows[r].splits);
            abort();
        }
        for (size_t cut = 1; cut < s.len; cut++) {
            check_cut(s, rows[r].std, cut, &w);
        }
    }
}

/* The editor's checkpoint inside a nested comment: the state there, and the
 * same tokens from it as from the start. */
static void checkpoint(void) {
    assert(sc_arena_reset(&ar));
    sc_str s = text("select /* one /* two */ three */ 1");
    sc_str head = sc_slice_sub(&ar, s, 0, 18);
    sqltok_state st = start(true);
    sqltok_token t = sqltok_step(&ar, head, 0, st, false);
    assert(t.kind == SQLTOK_WORD && t.end == 6);
    t = sqltok_step(&ar, head, 6, t.next, false);
    assert(t.kind == SQLTOK_SPACE && t.end == 7);
    t = sqltok_step(&ar, head, 7, t.next, false);
    assert(t.kind == SQLTOK_BLOCK_COMMENT && t.end == 18);
    assert(t.next.mode == SQLTOK_IN_COMMENT && t.next.depth == 2);
    sqltok_token u = sqltok_step(&ar, s, 18, t.next, true);
    assert(u.kind == SQLTOK_BLOCK_COMMENT && u.end == 32 && u.next.mode == SQLTOK_IN_NONE);
    u = sqltok_step(&ar, s, 32, u.next, true);
    assert(u.kind == SQLTOK_SPACE && u.end == 33);
    u = sqltok_step(&ar, s, 33, u.next, true);
    assert(u.kind == SQLTOK_NUMBER && u.end == 34);
}

/* A dollar string cut in the editor's window: the tag travels in the state
 * and a copy of it in another place still closes the string. */
static void dollar_tag(void) {
    assert(sc_arena_reset(&ar));
    sc_str s = text("$fn$ body $f $fn$;");
    sc_str head = sc_slice_sub(&ar, s, 0, 12);
    sqltok_token t = sqltok_step(&ar, head, 0, start(true), false);
    assert(t.kind == SQLTOK_DOLLAR && t.next.mode == SQLTOK_IN_DOLLAR);
    assert(t.end == 9 && t.next.tag.len == 2 && t.next.tag.ptr == s.ptr + 1);
    sc_str copy = text("fn");
    sqltok_state st = t.next;
    st.tag = copy;
    sqltok_token u = sqltok_step(&ar, s, t.end, st, true);
    assert(u.kind == SQLTOK_DOLLAR && u.end == 17 && u.next.mode == SQLTOK_IN_NONE);
    assert(u.next.tag.len == 0);
    sqltok_token none = sqltok_step(&ar, head, 9, st, false);
    assert(none.kind == SQLTOK_NONE && none.end == 9);
}

/* KTD25: the text lives in a scratch arena and the tag copy in the
 * checkpoint arena, so the step reads the tag through its own arena. */
static void tag_arena(void) {
    static alignas(8) uint8_t cmem[64];
    sc_arena cp;
    sc_arena_init(&cp, cmem, sizeof cmem);
    assert(sc_arena_reset(&ar));
    sc_str s = text("$fn$ body $f $fn$;");
    sc_str head = sc_slice_sub(&ar, s, 0, 12);
    sqltok_token t = sqltok_step(&ar, head, 0, start(true), false);
    assert(t.kind == SQLTOK_DOLLAR && t.end == 9 && t.next.tag.len == 2);
    sqltok_state st = t.next;
    st.tag = sc_str_from_lit(&cp, (const uint8_t *)"fn", 2);
    assert(st.tag.len == 2);
    sqltok_token u = sqltok_step(&ar, s, t.end, st, true);
    assert(u.kind == SQLTOK_DOLLAR && u.end == 17 && u.next.mode == SQLTOK_IN_NONE);
    assert(sqltok_step(&ar, s, u.end, u.next, true).kind == SQLTOK_OP);
}

/* Unterminated 'abc at the very end of a heap block: a read past it is an
 * ASan error. */
static void no_read_past(void) {
    uint8_t *block = malloc(8);
    assert(block);
    sc_arena h;
    sc_arena_init(&h, block, 8);
    sc_str whole = sc_str_from_lit(&h, (const uint8_t *)"xxxx'abc", 8);
    sc_str s = sc_slice_sub(&h, whole, 4, 4);
    sqltok_token t = sqltok_step(&h, s, 0, start(true), true);
    assert(t.kind == SQLTOK_UNTERMINATED && t.end == 4);
    assert(t.next.mode == SQLTOK_IN_QUOTE && t.next.quote == SQLTOK_Q_PLAIN);
    t = sqltok_step(&h, s, 0, start(true), false);
    assert(t.kind == SQLTOK_STRING && t.end == 4 && t.next.mode == SQLTOK_IN_QUOTE);
    assert(sqltok_stmt_end(&h, s, 0, true) == 4);
    free(block);
}

static void edges(void) {
    assert(sc_arena_reset(&ar));
    sc_str s = text("select 1;");
    sqltok_state st = start(true);
    sqltok_token t = sqltok_step(&ar, s, s.len, st, true);
    assert(t.kind == SQLTOK_NONE && t.end == s.len);
    t = sqltok_step(&ar, s, SIZE_MAX, st, true);
    assert(t.kind == SQLTOK_NONE && t.end == SIZE_MAX);
    assert(sqltok_stmt_end(&ar, s, SIZE_MAX, true) == s.len);
    assert(sqltok_stmt_end(&ar, s, 8, true) == 8);
    assert(sqltok_stmt_end(&ar, s, 9, true) == 9);

    sqltok_state c = start(true);
    c.mode = SQLTOK_IN_COMMENT;
    c.depth = SIZE_MAX;
    sc_str open = text("/* x");
    t = sqltok_step(&ar, open, 0, c, true);
    assert(t.kind == SQLTOK_UNTERMINATED && t.end == 4 && t.next.depth == SIZE_MAX);
    c.depth = 0;
    sc_str close = text("*/ x");
    t = sqltok_step(&ar, close, 0, c, true);
    assert(t.kind == SQLTOK_BLOCK_COMMENT && t.end == 2 && t.next.mode == SQLTOK_IN_NONE);

    sqltok_state q = start(true);
    q.mode = SQLTOK_IN_QUOTE;
    q.quote = SQLTOK_Q_IDENT;
    sc_str id = text("a\"\" b\" x");
    t = sqltok_step(&ar, id, 0, q, true);
    assert(t.kind == SQLTOK_QIDENT && t.end == 6 && t.next.mode == SQLTOK_IN_NONE);
    sc_str tail = text("'");
    q.quote = SQLTOK_Q_PLAIN;
    t = sqltok_step(&ar, tail, 0, q, false);
    assert(t.kind == SQLTOK_NONE && t.end == 0);
    t = sqltok_step(&ar, tail, 0, q, true);
    assert(t.kind == SQLTOK_STRING && t.end == 1);

    sqltok_state d = start(true);
    d.mode = SQLTOK_IN_DOLLAR;
    d.tag = text("tag");
    assert(sc_arena_reset(&ar));
    sc_str body = text("x $tag$ y");
    t = sqltok_step(&ar, body, 0, d, true);
    assert(t.kind == SQLTOK_NONE && t.end == 0);
    d.tag = text("tag");
    t = sqltok_step(&ar, body, 0, d, true);
    assert(t.kind == SQLTOK_DOLLAR && t.end == 7);
    assert(sc_arena_reset(&ar));
    assert(sqltok_stmt_end(&ar, body, 0, true) == body.len);
}

int main(void) {
    sc_arena_init(&ar, mem, sizeof mem);
    table();
    checkpoint();
    dollar_tag();
    tag_arena();
    no_read_past();
    edges();
    puts("explorer_sqltok_test: ok");
    return 0;
}
