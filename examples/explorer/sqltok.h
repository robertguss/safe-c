#ifndef EXPLORER_SQLTOK_H
#define EXPLORER_SQLTOK_H

/* The editor's SQL tokenizer and statement splitter. Core code: it includes
 * only the stdlib, so tests/explorer_sqltok_test.c links it alone.
 *
 * The lexer is resumable: one step lexes one token from `off` in a state and
 * returns its kind, its end and the state after it, so the editor can re-lex
 * from a saved checkpoint. The rules follow psql's lexer
 * (src/fe_utils/psqlscan.l); the logic functions below state them, and each
 * contract says the C computes exactly what they do. */

#include "safec/arena.h"
#include "safec/str.h"

/* Token kinds. */
#define SQLTOK_NONE ((uint8_t)0)          /* nothing lexed: end of input, or more is needed */
#define SQLTOK_SPACE ((uint8_t)1)         /* a run of space, tab, newline, CR, FF, VT */
#define SQLTOK_WORD ((uint8_t)2)          /* a keyword or unquoted identifier */
#define SQLTOK_QIDENT ((uint8_t)3)        /* "quoted identifier", U&"..." */
#define SQLTOK_STRING ((uint8_t)4)        /* '...', E'...', U&'...', N'...', B'...', X'...' */
#define SQLTOK_DOLLAR ((uint8_t)5)        /* $tag$ ... $tag$ */
#define SQLTOK_NUMBER ((uint8_t)6)        /* a number, with any trailing identifier junk */
#define SQLTOK_PARAM ((uint8_t)7)         /* $1 */
#define SQLTOK_LINE_COMMENT ((uint8_t)8)  /* -- up to the end of the line */
#define SQLTOK_BLOCK_COMMENT ((uint8_t)9) /* nested slash-star comment */
#define SQLTOK_OP ((uint8_t)10)           /* one byte of operator or punctuation */
#define SQLTOK_UNTERMINATED ((uint8_t)11) /* a string or comment still open at end of input */

/* Lexer modes: what the state is inside of. */
#define SQLTOK_IN_NONE ((uint8_t)0)
#define SQLTOK_IN_QUOTE ((uint8_t)1)
#define SQLTOK_IN_COMMENT ((uint8_t)2)
#define SQLTOK_IN_DOLLAR ((uint8_t)3)

/* Quote kinds, for SQLTOK_IN_QUOTE. */
#define SQLTOK_Q_PLAIN ((uint8_t)0) /* '...': backslash escapes when std_strings is off */
#define SQLTOK_Q_EXT ((uint8_t)1)   /* E'...': backslash always escapes */
#define SQLTOK_Q_UNI ((uint8_t)2)   /* U&'...': no backslash escape */
#define SQLTOK_Q_BIT ((uint8_t)3)   /* B'...', X'...': no '' doubling */
#define SQLTOK_Q_IDENT ((uint8_t)4) /* "..." and U&"..." */

/* The splitter's word kinds and how far a statement has matched
 * CREATE [OR REPLACE] FUNCTION|PROCEDURE, as psql tracks them. */
#define SQLTOK_W_OTHER ((uint8_t)0)
#define SQLTOK_W_CREATE ((uint8_t)1)
#define SQLTOK_W_OR ((uint8_t)2)
#define SQLTOK_W_REPLACE ((uint8_t)3)
#define SQLTOK_W_ROUTINE ((uint8_t)4) /* FUNCTION or PROCEDURE */
#define SQLTOK_W_BEGIN ((uint8_t)5)
#define SQLTOK_W_CASE ((uint8_t)6)
#define SQLTOK_W_END ((uint8_t)7)
#define SQLTOK_P_START ((uint8_t)0)
#define SQLTOK_P_CREATE ((uint8_t)1)
#define SQLTOK_P_OR ((uint8_t)2)
#define SQLTOK_P_REPLACE ((uint8_t)3)
#define SQLTOK_P_ROUTINE ((uint8_t)4) /* the whole prefix: BEGIN ... END nests */
#define SQLTOK_P_NO ((uint8_t)5)      /* some other statement */

/* `depth` is the block-comment nesting; `tag` the open dollar tag, read
 * through `tag_arena` (a step that opens one points it into the text's arena;
 * the caller may move a copy elsewhere and point `tag_arena` there);
 * `std_strings` is standard_conforming_strings. A zeroed state with
 * std_strings set is the start of input. */
typedef struct {
    uint8_t mode;
    size_t depth;
    uint8_t quote;
    sc_str tag;
    const sc_arena *tag_arena;
    bool std_strings;
} sqltok_state;

typedef struct {
    uint8_t kind;
    size_t end;
    sqltok_state next;
} sqltok_token;

/*@ axiomatic sqltok_blank { logic sqltok_token tk_blank; }

    logic integer tk_at(sc_str s, integer i) = 0 <= i < s.len ? s.ptr[i] : 0;
    predicate tk_space(integer b) =
      b == ' ' || b == '\t' || b == '\n' || b == '\r' || b == '\f' || b == '\v';
    predicate tk_nl(integer b) = b == '\n' || b == '\r';
    predicate tk_digit(integer b) = '0' <= b <= '9';
    predicate tk_istart(integer b) = 'a' <= b <= 'z' || 'A' <= b <= 'Z' || b == '_' || b >= 0x80;
    predicate tk_dcont(integer b) = tk_istart(b) || tk_digit(b);
    predicate tk_icont(integer b) = tk_dcont(b) || b == '$';

    logic integer tk_spaces(sc_str s, integer i) = tk_space(tk_at(s, i)) ? tk_spaces(s, i + 1) : i;
    logic integer tk_ident(sc_str s, integer i) = tk_icont(tk_at(s, i)) ? tk_ident(s, i + 1) : i;
    logic integer tk_tagrun(sc_str s, integer i) = tk_dcont(tk_at(s, i)) ? tk_tagrun(s, i + 1) : i;
    logic integer tk_digits(sc_str s, integer i) = tk_digit(tk_at(s, i)) ? tk_digits(s, i + 1) : i;
    logic integer tk_line(sc_str s, integer i) =
      i >= s.len || tk_nl(tk_at(s, i)) ? i : tk_line(s, i + 1);
    logic integer tk_dec(sc_str s, integer i) =
      tk_digit(tk_at(s, i)) || (tk_at(s, i) == '_' && tk_digit(tk_at(s, i + 1))) ? tk_dec(s, i + 1) : i;
    logic integer tk_decs(sc_str s, integer i) = tk_digit(tk_at(s, i)) ? tk_dec(s, i) : i;

    logic sqltok_token tk_tok(integer kind, integer end, sqltok_state next) =
      { { { tk_blank \with .kind = (uint8_t)kind } \with .end = (size_t)end } \with .next = next };
    logic sqltok_state tk_closed(sqltok_state st) =
      { { { { st \with .mode = SQLTOK_IN_NONE } \with .depth = (size_t)0 }
          \with .quote = SQLTOK_Q_PLAIN } \with .tag = { st.tag \with .len = (size_t)0 } };
    logic integer tk_open_kind(sqltok_state st) =
      st.mode == SQLTOK_IN_COMMENT ? SQLTOK_BLOCK_COMMENT :
      st.mode == SQLTOK_IN_DOLLAR ? SQLTOK_DOLLAR :
      st.quote == SQLTOK_Q_IDENT ? SQLTOK_QIDENT : SQLTOK_STRING;
    logic sqltok_token tk_part(integer start, integer stop, sqltok_state st) =
      stop == start ? tk_tok(SQLTOK_NONE, start, st) : tk_tok(tk_open_kind(st), stop, st);
    logic sqltok_token tk_open_end(integer start, integer i, sqltok_state st, integer ae) =
      ae != 0 ? tk_tok(SQLTOK_UNTERMINATED, i, st) : tk_part(start, i, st);
    logic sqltok_token tk_simple(integer kind, integer off, integer end, integer look, integer len,
                                 sqltok_state st, integer ae) =
      ae == 0 && look >= len ? tk_tok(SQLTOK_NONE, off, st) : tk_tok(kind, end, st);

    logic integer tk_gap(sc_str s, integer i) =
      tk_space(tk_at(s, i)) ? tk_gap(s, i + 1) :
      tk_at(s, i) == '-' && tk_at(s, i + 1) == '-' ? tk_gap(s, tk_line(s, i + 1)) : i;
    predicate tk_gap_nl(sc_str s, integer i) =
      tk_space(tk_at(s, i)) ? tk_nl(tk_at(s, i)) || tk_gap_nl(s, i + 1) :
      tk_at(s, i) == '-' && tk_at(s, i + 1) == '-' ? tk_gap_nl(s, tk_line(s, i + 1)) : \false;
    predicate tk_gap_more(sc_str s, integer i) =
      tk_gap(s, i) >= s.len || (tk_at(s, tk_gap(s, i)) == '-' && tk_gap(s, i) + 1 >= s.len);
    predicate tk_gap_quote(sc_str s, integer i) =
      tk_gap_nl(s, i) && tk_at(s, tk_gap(s, i)) == '\'';

    logic integer tk_qc(integer q) = q == SQLTOK_Q_IDENT ? '"' : '\'';
    predicate tk_qesc(integer q, integer std) =
      q == SQLTOK_Q_EXT || (q == SQLTOK_Q_PLAIN && std == 0);
    logic integer tk_qend(sc_str s, integer i, integer q, integer std, integer ae) =
      i >= s.len ? i :
      tk_at(s, i) == tk_qc(q) ?
        (q != SQLTOK_Q_BIT && tk_at(s, i + 1) == tk_qc(q) ?
           tk_qend(s, i + 2, q, std, ae) :
         q != SQLTOK_Q_IDENT && !(tk_gap_more(s, i + 1) && ae == 0) && tk_gap_quote(s, i + 1) ?
           tk_qend(s, tk_gap(s, i + 1) + 1, q, std, ae) :
         i) :
      tk_at(s, i) == '\\' && tk_qesc(q, std) ?
        (i + 1 < s.len ? tk_qend(s, i + 2, q, std, ae) : ae == 0 ? i : i + 1) :
      tk_qend(s, i + 1, q, std, ae);
    predicate tk_qclose(sc_str s, integer j, integer q, integer ae) =
      tk_at(s, j) == tk_qc(q) &&
      (q == SQLTOK_Q_IDENT ? j + 1 < s.len || ae != 0 : !tk_gap_more(s, j + 1) || ae != 0);
    logic sqltok_token tk_quote(sc_str s, integer start, integer i, sqltok_state st, integer ae) =
      \let j = tk_qend(s, i, st.quote, st.std_strings, ae);
      j >= s.len ? tk_open_end(start, j, st, ae) :
      tk_qclose(s, j, st.quote, ae) ?
        tk_tok(st.quote == SQLTOK_Q_IDENT ? SQLTOK_QIDENT : SQLTOK_STRING, j + 1, tk_closed(st)) :
      tk_part(start, j, st);

    predicate tk_cstop(sc_str s, integer i, integer ae) =
      (tk_at(s, i) == '*' || tk_at(s, i) == '/') && i + 1 >= s.len && ae == 0;
    predicate tk_cclose(sc_str s, integer i) = tk_at(s, i) == '*' && tk_at(s, i + 1) == '/';
    predicate tk_copen(sc_str s, integer i) = tk_at(s, i) == '/' && tk_at(s, i + 1) == '*';
    logic integer tk_deeper(integer d) = d < SIZE_MAX ? d + 1 : d;
    logic integer tk_cend(sc_str s, integer i, integer d, integer ae) =
      i >= s.len || tk_cstop(s, i, ae) ? i :
      tk_cclose(s, i) ? (d <= 1 ? i : tk_cend(s, i + 2, d - 1, ae)) :
      tk_copen(s, i) ? tk_cend(s, i + 2, tk_deeper(d), ae) :
      tk_cend(s, i + 1, d, ae);
    logic integer tk_cdepth(sc_str s, integer i, integer d, integer ae) =
      i >= s.len || tk_cstop(s, i, ae) ? d :
      tk_cclose(s, i) ? (d <= 1 ? d : tk_cdepth(s, i + 2, d - 1, ae)) :
      tk_copen(s, i) ? tk_cdepth(s, i + 2, tk_deeper(d), ae) :
      tk_cdepth(s, i + 1, d, ae);
    logic sqltok_token tk_comment(sc_str s, integer start, integer i, sqltok_state st, integer ae) =
      \let j = tk_cend(s, i, st.depth, ae);
      \let n = { st \with .depth = (size_t)tk_cdepth(s, i, st.depth, ae) };
      j >= s.len ? tk_open_end(start, j, n, ae) :
      tk_cclose(s, j) ? tk_tok(SQLTOK_BLOCK_COMMENT, j + 2, tk_closed(n)) :
      tk_part(start, j, n);

    predicate tk_closes(sc_str s, integer i, sc_str tag) =
      s.len - i - 1 > tag.len && tk_at(s, i + 1 + tag.len) == '$' &&
      \forall integer k; 0 <= k < tag.len ==> tk_at(s, i + 1 + k) == tag.ptr[k];
    predicate tk_dhold(sc_str s, integer i, sc_str tag, integer ae) =
      ae == 0 && s.len - i - 1 <= tag.len;
    logic integer tk_dend(sc_str s, integer i, sc_str tag, integer ae) =
      i >= s.len || tk_dhold(s, i, tag, ae) || (tk_at(s, i) == '$' && tk_closes(s, i, tag)) ? i :
      tk_dend(s, i + 1, tag, ae);
    logic sqltok_token tk_dollar(sc_str s, integer start, integer i, sqltok_state st, integer ae) =
      st.tag.len > 0 && !valid_slice(st.tag_arena, st.tag) ? tk_tok(SQLTOK_NONE, start, st) :
      \let j = tk_dend(s, i, st.tag, ae);
      j >= s.len ? tk_open_end(start, j, st, ae) :
      tk_dhold(s, j, st.tag, ae) ? tk_part(start, j, st) :
      tk_tok(SQLTOK_DOLLAR, j + st.tag.len + 2, tk_closed(st));

    logic integer tk_frac(sc_str s, integer off) =
      \let i = tk_decs(s, off);
      tk_at(s, off) == '.' ? tk_decs(s, off + 1) :
      tk_at(s, i) == '.' && tk_at(s, i + 1) != '.' ? tk_decs(s, i + 1) : i;
    predicate tk_exp(sc_str s, integer i) = tk_at(s, i) == 'e' || tk_at(s, i) == 'E';
    predicate tk_sign(integer b) = b == '+' || b == '-';
    logic integer tk_junk(sc_str s, integer off, integer q, integer lim, integer end) =
      q >= lim ? end :
      tk_istart(tk_at(s, q)) &&
      (tk_digit(tk_at(s, q - 1)) || (tk_at(s, q - 1) == '.' && q - 1 > off)) ?
        tk_junk(s, off, tk_ident(s, q + 1), lim, tk_ident(s, q + 1) > end ? tk_ident(s, q + 1) : end) :
      tk_junk(s, off, q + 1, lim, end);
    predicate tk_early(sc_str s, integer i) =
      tk_exp(s, i) && tk_sign(tk_at(s, i + 1)) && !tk_digit(tk_at(s, i + 2));
    logic integer tk_exp_end(sc_str s, integer i) =
      !tk_exp(s, i) ? i :
      tk_digit(tk_at(s, i + 1)) ? tk_decs(s, i + 1) :
      tk_sign(tk_at(s, i + 1)) ? tk_decs(s, i + 2) : i;
    logic integer tk_tail(sc_str s, integer off, integer i) =
      \let k = tk_istart(tk_at(s, i)) ? tk_ident(s, i + 1) : i;
      tk_junk(s, off, off + 1, k, k);
    logic integer tk_number(sc_str s, integer off) =
      tk_early(s, tk_frac(s, off)) ? tk_frac(s, off) + 2 : tk_tail(s, off, tk_exp_end(s, tk_frac(s, off)));

    logic sqltok_state tk_in_quote(sqltok_state st, integer q) =
      { { st \with .mode = SQLTOK_IN_QUOTE } \with .quote = (uint8_t)q };
    logic sqltok_token tk_lex_word(sc_str s, integer off, sqltok_state st, integer ae) =
      \let b = tk_at(s, off);
      \let c = tk_at(s, off + 1);
      c == '\'' && (b == 'e' || b == 'E') ? tk_quote(s, off, off + 2, tk_in_quote(st, SQLTOK_Q_EXT), ae) :
      c == '\'' && (b == 'b' || b == 'B' || b == 'x' || b == 'X') ?
        tk_quote(s, off, off + 2, tk_in_quote(st, SQLTOK_Q_BIT), ae) :
      c == '\'' && (b == 'n' || b == 'N') ? tk_quote(s, off, off + 2, tk_in_quote(st, SQLTOK_Q_PLAIN), ae) :
      (b == 'u' || b == 'U') && c == '&' ?
        (tk_at(s, off + 2) == '\'' ? tk_quote(s, off, off + 3, tk_in_quote(st, SQLTOK_Q_UNI), ae) :
         tk_at(s, off + 2) == '"' ? tk_quote(s, off, off + 3, tk_in_quote(st, SQLTOK_Q_IDENT), ae) :
         tk_simple(SQLTOK_OP, off, off + 1, off + 2, s.len, st, ae)) :
      tk_simple(SQLTOK_WORD, off, tk_ident(s, off + 1), tk_ident(s, off + 1), s.len, st, ae);

    logic sc_str tk_tag(sc_str s, integer off, integer t) =
      t - off - 1 > 0 ? { { s \with .ptr = s.ptr + (off + 1) } \with .len = (size_t)(t - off - 1) } :
      { { s \with .ptr = \null } \with .len = (size_t)0 };
    logic sqltok_token tk_lex_dollar(sc_arena *a, sc_str s, integer off, sqltok_state st, integer ae) =
      \let b = tk_at(s, off + 1);
      \let d = tk_digits(s, off + 2);
      \let p = tk_istart(tk_at(s, d)) ? tk_ident(s, d + 1) : d;
      \let t = tk_istart(b) ? tk_tagrun(s, off + 2) : off + 1;
      tk_digit(b) ? tk_simple(SQLTOK_PARAM, off, p, p, s.len, st, ae) :
      tk_at(s, t) != '$' ? tk_simple(SQLTOK_OP, off, off + 1, t, s.len, st, ae) :
      tk_dollar(s, off, t + 1,
                { { { st \with .mode = SQLTOK_IN_DOLLAR } \with .tag = tk_tag(s, off, t) }
                  \with .tag_arena = a }, ae);

    logic sqltok_token tk_lex(sc_arena *a, sc_str s, integer off, sqltok_state st, integer ae) =
      \let b = tk_at(s, off);
      \let c = tk_at(s, off + 1);
      tk_space(b) ? tk_simple(SQLTOK_SPACE, off, tk_spaces(s, off + 1), tk_spaces(s, off + 1), s.len, st, ae) :
      b == '-' && c == '-' ?
        tk_simple(SQLTOK_LINE_COMMENT, off, tk_line(s, off + 2), tk_line(s, off + 2), s.len, st, ae) :
      b == '/' && c == '*' ?
        tk_comment(s, off, off + 2, { { st \with .mode = SQLTOK_IN_COMMENT } \with .depth = (size_t)1 }, ae) :
      b == '\'' ? tk_quote(s, off, off + 1, tk_in_quote(st, SQLTOK_Q_PLAIN), ae) :
      b == '"' ? tk_quote(s, off, off + 1, tk_in_quote(st, SQLTOK_Q_IDENT), ae) :
      b == '$' ? tk_lex_dollar(a, s, off, st, ae) :
      tk_digit(b) || (b == '.' && tk_digit(c)) ?
        tk_simple(SQLTOK_NUMBER, off, tk_number(s, off), tk_number(s, off), s.len, st, ae) :
      tk_istart(b) ? tk_lex_word(s, off, st, ae) :
      tk_simple(SQLTOK_OP, off, off + 1, b == '-' || b == '/' || b == '.' ? off + 1 : off, s.len, st, ae);

    logic sqltok_token tk_step(sc_arena *a, sc_str s, integer off, sqltok_state st, integer ae) =
      !valid_slice(a, s) || off >= s.len ? tk_tok(SQLTOK_NONE, off, st) :
      st.mode == SQLTOK_IN_QUOTE ? tk_quote(s, off, off, st, ae) :
      st.mode == SQLTOK_IN_COMMENT ? tk_comment(s, off, off, st, ae) :
      st.mode == SQLTOK_IN_DOLLAR ? tk_dollar(s, off, off, st, ae) :
      tk_lex(a, s, off, st, ae);

    predicate tag_ok(sqltok_state st) =
      st.mode == SQLTOK_IN_DOLLAR && st.tag.len > 0 ==>
        valid_arena(st.tag_arena) && slice_storage(st.tag_arena, st.tag);
    predicate sqltok_result(sc_arena *a, sc_str s, size_t off, sqltok_state st, sqltok_token t) =
      tag_ok(t.next) && t.kind <= SQLTOK_UNTERMINATED &&
      (t.kind == SQLTOK_NONE ==> t.end == off && t.next == st) &&
      (t.kind != SQLTOK_NONE ==> valid_slice(a, s) && off < t.end <= s.len);
*/

/* Lexes one token of `s` from `off` in state `st`. `at_end` says that `s`
 * ends the input. Without it, a string, identifier, dollar string or block
 * comment still open at the end of `s` comes back as its kind up to where
 * more input could change it, with the open state in `next`; any other token
 * that could still grow comes back as SQLTOK_NONE. With it, an open construct
 * is SQLTOK_UNTERMINATED up to the end, with the open state in `next`.
 * SQLTOK_NONE has end == off and next == st; it is also the result at or past
 * the end, for a stale `s`, and in a dollar string whose tag is stale.
 * Any other kind is a non-empty token inside `s` (off < end <= s.len), so
 * stepping from each token's end tiles `s` with no gap or overlap, and with
 * `at_end` every step inside a live `s` makes progress. The caller
 * guarantees `s` is storage of `a` and an open tag storage of st.tag_arena. */
/*@ requires valid_arena(a) && slice_storage(a, s) && tag_ok(st);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures sqltok_result(a, s, off, st, \result);
    ensures \result == tk_step(a, s, off, st, at_end);
    ensures at_end && valid_slice(a, s) && off < s.len &&
            !(st.mode == SQLTOK_IN_DOLLAR && st.tag.len > 0 && !valid_slice(st.tag_arena, st.tag)) ==>
            \result.kind != SQLTOK_NONE;
*/
sqltok_token sqltok_step(const sc_arena *a, sc_str s, size_t off, sqltok_state st, bool at_end);

/*@ predicate tk_ci(sc_str s, integer i, integer c) = tk_at(s, i) == c || tk_at(s, i) == c - 0x20;
    predicate tk_w2(sc_str s, integer o, integer n, integer c0, integer c1) =
      n == 2 && tk_ci(s, o, c0) && tk_ci(s, o + 1, c1);
    predicate tk_w3(sc_str s, integer o, integer n, integer c0, integer c1, integer c2) =
      n == 3 && tk_ci(s, o, c0) && tk_ci(s, o + 1, c1) && tk_ci(s, o + 2, c2);
    predicate tk_w4(sc_str s, integer o, integer n, integer c0, integer c1, integer c2, integer c3) =
      n == 4 && tk_ci(s, o, c0) && tk_ci(s, o + 1, c1) && tk_ci(s, o + 2, c2) && tk_ci(s, o + 3, c3);
    predicate tk_w5(sc_str s, integer o, integer n, integer c0, integer c1, integer c2, integer c3, integer c4) =
      n == 5 && tk_ci(s, o, c0) && tk_ci(s, o + 1, c1) && tk_ci(s, o + 2, c2) && tk_ci(s, o + 3, c3) && tk_ci(s, o + 4, c4);
    predicate tk_w6(sc_str s, integer o, integer n, integer c0, integer c1, integer c2, integer c3, integer c4, integer c5) =
      n == 6 && tk_ci(s, o, c0) && tk_ci(s, o + 1, c1) && tk_ci(s, o + 2, c2) && tk_ci(s, o + 3, c3) && tk_ci(s, o + 4, c4) && tk_ci(s, o + 5, c5);
    predicate tk_w7(sc_str s, integer o, integer n, integer c0, integer c1, integer c2, integer c3, integer c4, integer c5, integer c6) =
      n == 7 && tk_ci(s, o, c0) && tk_ci(s, o + 1, c1) && tk_ci(s, o + 2, c2) && tk_ci(s, o + 3, c3) && tk_ci(s, o + 4, c4) && tk_ci(s, o + 5, c5) && tk_ci(s, o + 6, c6);
    predicate tk_w8(sc_str s, integer o, integer n, integer c0, integer c1, integer c2, integer c3, integer c4, integer c5, integer c6, integer c7) =
      n == 8 && tk_ci(s, o, c0) && tk_ci(s, o + 1, c1) && tk_ci(s, o + 2, c2) && tk_ci(s, o + 3, c3) && tk_ci(s, o + 4, c4) && tk_ci(s, o + 5, c5) && tk_ci(s, o + 6, c6) && tk_ci(s, o + 7, c7);
    predicate tk_w9(sc_str s, integer o, integer n, integer c0, integer c1, integer c2, integer c3, integer c4, integer c5, integer c6, integer c7, integer c8) =
      n == 9 && tk_ci(s, o, c0) && tk_ci(s, o + 1, c1) && tk_ci(s, o + 2, c2) && tk_ci(s, o + 3, c3) && tk_ci(s, o + 4, c4) && tk_ci(s, o + 5, c5) && tk_ci(s, o + 6, c6) && tk_ci(s, o + 7, c7) && tk_ci(s, o + 8, c8);
    logic integer tk_word(sc_str s, integer o, integer n) =
      tk_w2(s, o, n, 'o', 'r') ? SQLTOK_W_OR :
      tk_w3(s, o, n, 'e', 'n', 'd') ? SQLTOK_W_END :
      tk_w4(s, o, n, 'c', 'a', 's', 'e') ? SQLTOK_W_CASE :
      tk_w5(s, o, n, 'b', 'e', 'g', 'i', 'n') ? SQLTOK_W_BEGIN :
      tk_w6(s, o, n, 'c', 'r', 'e', 'a', 't', 'e') ? SQLTOK_W_CREATE :
      tk_w7(s, o, n, 'r', 'e', 'p', 'l', 'a', 'c', 'e') ? SQLTOK_W_REPLACE :
      tk_w8(s, o, n, 'f', 'u', 'n', 'c', 't', 'i', 'o', 'n') ? SQLTOK_W_ROUTINE :
      tk_w9(s, o, n, 'p', 'r', 'o', 'c', 'e', 'd', 'u', 'r', 'e') ? SQLTOK_W_ROUTINE :
      SQLTOK_W_OTHER;
    logic integer tk_prefix(integer p, integer w) =
      p == SQLTOK_P_START && w == SQLTOK_W_CREATE ? SQLTOK_P_CREATE :
      p == SQLTOK_P_CREATE && w == SQLTOK_W_OR ? SQLTOK_P_OR :
      p == SQLTOK_P_OR && w == SQLTOK_W_REPLACE ? SQLTOK_P_REPLACE :
      (p == SQLTOK_P_CREATE || p == SQLTOK_P_REPLACE) && w == SQLTOK_W_ROUTINE ? SQLTOK_P_ROUTINE :
      p == SQLTOK_P_ROUTINE ? SQLTOK_P_ROUTINE : SQLTOK_P_NO;
    logic integer tk_begin(integer d, integer w) =
      w == SQLTOK_W_BEGIN && d < SIZE_MAX ? d + 1 :
      w == SQLTOK_W_CASE && d >= 1 && d < SIZE_MAX ? d + 1 :
      w == SQLTOK_W_END && d > 0 ? d - 1 : d;
    logic integer tk_paren(sc_str s, integer off, integer p) =
      tk_at(s, off) == '(' && p < SIZE_MAX ? p + 1 : tk_at(s, off) == ')' && p > 0 ? p - 1 : p;

    logic sqltok_state tk_start(sc_arena *a, integer std) =
      { { { { { { tk_blank.next \with .mode = SQLTOK_IN_NONE } \with .depth = (size_t)0 }
              \with .quote = SQLTOK_Q_PLAIN }
            \with .tag = { { { tk_blank.next.tag \with .ptr = \null } \with .len = (size_t)0 }
                           \with .epoch = a->epoch } }
          \with .tag_arena = a } \with .std_strings = (_Bool)std };
    logic integer tk_walk(sc_arena *a, sc_str s, integer off, sqltok_token t, integer paren,
                          integer begin, integer prefix) =
      t.kind == SQLTOK_NONE ? s.len :
      t.kind == SQLTOK_OP && tk_at(s, off) == ';' && paren == 0 && begin == 0 ? off :
      \let u = tk_step(a, s, t.end, t.next, 1);
      t.kind == SQLTOK_OP ? tk_walk(a, s, t.end, u, tk_paren(s, off, paren), begin, prefix) :
      t.kind == SQLTOK_WORD && paren == 0 ?
        (\let w = tk_word(s, off, t.end - off);
         \let p = tk_prefix(prefix, w);
         tk_walk(a, s, t.end, u, paren, p == SQLTOK_P_ROUTINE ? tk_begin(begin, w) : begin, p)) :
      tk_walk(a, s, t.end, u, paren, begin, prefix);
    logic integer tk_split(sc_arena *a, sc_str s, integer from, integer std) =
      tk_walk(a, s, from, tk_step(a, s, from, tk_start(a, std), 1), 0, 0, SQLTOK_P_START);
*/

/* The offset of the `;` that ends the statement starting at `from`, or s.len
 * when none does. A `;` ends a statement only outside parentheses and outside
 * BEGIN ... END in CREATE [OR REPLACE] FUNCTION or PROCEDURE: tk_split walks
 * the tokens from `from` and stops at the first such `;` token. `std` is
 * standard_conforming_strings. A `from` past s.len gives s.len. The caller
 * guarantees `s` is storage of `a`. */
/*@ requires valid_arena(a) && slice_storage(a, s);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures from <= s.len ==> from <= \result <= s.len;
    ensures from > s.len ==> \result == s.len;
    ensures \result != s.len ==> valid_slice(a, s) && s.ptr[\result] == ';';
    ensures \result == tk_split(a, s, from, std);
*/
size_t sqltok_stmt_end(const sc_arena *a, sc_str s, size_t from, bool std);

#endif
