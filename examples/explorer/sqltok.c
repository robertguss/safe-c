#include "sqltok.h"

/*@ requires valid_arena(a) && valid_slice(a, s);
    assigns \nothing;
    ensures \result == tk_at(s, i);
*/
static uint8_t byte_at(const sc_arena *a, sc_str s, size_t i) {
    uint8_t b = 0;
    return sc_slice_get(a, s, i, &b) ? b : 0;
}

/*@ assigns \nothing;
    ensures \result <==> lo <= b <= hi;
*/
static bool between(uint8_t b, uint8_t lo, uint8_t hi) {
    return b >= lo && b <= hi;
}

/*@ assigns \nothing;
    ensures \result <==> tk_space(b);
*/
static bool is_space(uint8_t b) {
    return b == ' ' || b == '\t' || b == '\n' || b == '\r' || b == '\f' || b == '\v';
}

/*@ assigns \nothing;
    ensures \result <==> tk_digit(b);
*/
static bool is_digit(uint8_t b) {
    return between(b, '0', '9');
}

/*@ assigns \nothing;
    ensures \result <==> tk_istart(b);
*/
static bool ident_start(uint8_t b) {
    return between(b, 'a', 'z') || between(b, 'A', 'Z') || b == '_' || b >= 0x80;
}

/*@ assigns \nothing;
    ensures \result <==> tk_dcont(b);
*/
static bool dolq_cont(uint8_t b) {
    return ident_start(b) || is_digit(b);
}

/*@ assigns \nothing;
    ensures \result <==> tk_icont(b);
*/
static bool ident_cont(uint8_t b) {
    return dolq_cont(b) || b == '$';
}

/*@ assigns \nothing;
    ensures \result == tk_tok(kind, end, next);
*/
static sqltok_token tok(uint8_t kind, size_t end, sqltok_state next) {
    sqltok_token t;
    t.kind = kind;
    t.end = end;
    t.next = next;
    return t;
}

/*@ assigns \nothing;
    ensures \result == tk_closed(st);
*/
static sqltok_state closed(sqltok_state st) {
    st.mode = SQLTOK_IN_NONE;
    st.depth = 0;
    st.quote = SQLTOK_Q_PLAIN;
    st.tag.len = 0;
    return st;
}

/*@ assigns \nothing;
    ensures \result == tk_open_kind(st);
*/
static uint8_t open_kind(sqltok_state st) {
    if (st.mode == SQLTOK_IN_COMMENT) {
        return SQLTOK_BLOCK_COMMENT;
    }
    if (st.mode == SQLTOK_IN_DOLLAR) {
        return SQLTOK_DOLLAR;
    }
    return st.quote >= SQLTOK_Q_IDENT ? SQLTOK_QIDENT : SQLTOK_STRING;
}

/*@ assigns \nothing;
    ensures \result == tk_part(start, stop, st);
*/
static sqltok_token open_part(size_t start, size_t stop, sqltok_state st) {
    if (stop == start) {
        return tok(SQLTOK_NONE, start, st);
    }
    return tok(open_kind(st), stop, st);
}

/*@ assigns \nothing;
    ensures \result == tk_open_end(start, i, st, at_end);
*/
static sqltok_token open_end(size_t start, size_t i, sqltok_state st, bool at_end) {
    if (at_end) {
        return tok(SQLTOK_UNTERMINATED, i, st);
    }
    return open_part(start, i, st);
}

/*@ requires kind != SQLTOK_NONE;
    assigns \nothing;
    ensures \result == tk_simple(kind, off, end, look, len, st, at_end);
    ensures \result.next == st;
    ensures \result.kind == SQLTOK_NONE ==> \result.end == off;
    ensures \result.kind != SQLTOK_NONE ==> \result.kind == kind && \result.end == end;
*/
static sqltok_token simple(uint8_t kind, size_t off, size_t end, size_t look, size_t len,
                           sqltok_state st, bool at_end) {
    if (!at_end && look >= len) {
        return tok(SQLTOK_NONE, off, st);
    }
    return tok(kind, end, st);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && i <= s.len;
    assigns \nothing;
    ensures i <= \result <= s.len && \result == tk_line(s, i);
*/
static size_t run_line(const sc_arena *a, sc_str s, size_t i) {
    uint8_t b = 0;
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop invariant tk_line(s, i) == tk_line(s, \at(i, Pre));
        loop assigns i, b;
        loop variant s.len - i;
    */
    while (sc_slice_get(a, s, i, &b) && b != '\n' && b != '\r') {
        i++;
    }
    return i;
}

/*@ requires valid_arena(a) && valid_slice(a, s) && i <= s.len;
    assigns \nothing;
    ensures i <= \result <= s.len && \result == tk_spaces(s, i);
*/
static size_t run_space(const sc_arena *a, sc_str s, size_t i) {
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop invariant tk_spaces(s, i) == tk_spaces(s, \at(i, Pre));
        loop assigns i;
        loop variant s.len - i;
    */
    while (is_space(byte_at(a, s, i))) {
        i++;
    }
    return i;
}

/*@ requires valid_arena(a) && valid_slice(a, s) && i <= s.len;
    assigns \nothing;
    ensures i <= \result <= s.len && \result == tk_ident(s, i);
*/
static size_t run_ident(const sc_arena *a, sc_str s, size_t i) {
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop invariant tk_ident(s, i) == tk_ident(s, \at(i, Pre));
        loop assigns i;
        loop variant s.len - i;
    */
    while (ident_cont(byte_at(a, s, i))) {
        i++;
    }
    return i;
}

/*@ requires valid_arena(a) && valid_slice(a, s) && i <= s.len;
    assigns \nothing;
    ensures i <= \result <= s.len && \result == tk_tagrun(s, i);
*/
static size_t run_tag(const sc_arena *a, sc_str s, size_t i) {
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop invariant tk_tagrun(s, i) == tk_tagrun(s, \at(i, Pre));
        loop assigns i;
        loop variant s.len - i;
    */
    while (dolq_cont(byte_at(a, s, i))) {
        i++;
    }
    return i;
}

/*@ requires valid_arena(a) && valid_slice(a, s) && i <= s.len;
    assigns \nothing;
    ensures i <= \result <= s.len && \result == tk_pbody(s, i);
*/
static size_t parameter_body(const sc_arena *a, sc_str s, size_t i) {
    uint8_t b = byte_at(a, s, i);
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop invariant b == tk_at(s, i);
        loop invariant tk_pbody(s, i) == tk_pbody(s, \at(i, Pre));
        loop assigns i, b;
        loop variant s.len - i;
    */
    while (b != 0 && b != ')' && !is_space(b)) {
        i++;
        b = byte_at(a, s, i);
    }
    return b == ')' ? i + 1u : i;
}

/*@ requires valid_arena(a) && valid_slice(a, s) && i <= s.len;
    assigns \nothing;
    ensures i <= \result <= s.len && \result == tk_parameter(s, i, 0);
*/
static size_t run_parameter(const sc_arena *a, sc_str s, size_t i) {
    bool named = false;
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop invariant tk_parameter(s, i, named) == tk_parameter(s, \at(i, Pre), 0);
        loop assigns i, named;
        loop variant s.len - i;
    */
    while (i < s.len) {
        uint8_t b = byte_at(a, s, i);
        if (ident_cont(b)) {
            named = true;
            i++;
        } else if (b == '(' && named) {
            return parameter_body(a, s, i + 1);
        } else if (b == ':' && byte_at(a, s, i + 1) == ':') {
            i += 2;
        } else {
            break;
        }
    }
    return i;
}

/*@ requires valid_arena(a) && valid_slice(a, s) && i <= s.len;
    assigns \nothing;
    ensures i <= \result <= s.len && \result == tk_digits(s, i);
*/
static size_t run_plain_digits(const sc_arena *a, sc_str s, size_t i) {
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop invariant tk_digits(s, i) == tk_digits(s, \at(i, Pre));
        loop assigns i;
        loop variant s.len - i;
    */
    while (is_digit(byte_at(a, s, i))) {
        i++;
    }
    return i;
}

/*@ requires valid_arena(a) && valid_slice(a, s) && i <= s.len;
    assigns \nothing;
    ensures i <= \result <= s.len && \result == tk_decs(s, i);
    ensures tk_digit(tk_at(s, i)) ==> i < \result;
*/
static size_t run_digits(const sc_arena *a, sc_str s, size_t i) {
    if (!is_digit(byte_at(a, s, i))) {
        return i;
    }
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop invariant tk_dec(s, i) == tk_dec(s, \at(i, Pre));
        loop invariant i == \at(i, Pre) || \at(i, Pre) < i;
        loop assigns i;
        loop variant s.len - i;
    */
    while (is_digit(byte_at(a, s, i)) || (byte_at(a, s, i) == '_' && is_digit(byte_at(a, s, i + 1)))) {
        i++;
    }
    return i;
}

typedef struct {
    size_t end;
    bool nl;
} gap;

/*@ requires valid_arena(a) && valid_slice(a, s) && i <= s.len;
    assigns \nothing;
    ensures i <= \result.end <= s.len && \result.end == tk_gap(s, i);
    ensures \result.nl <==> tk_gap_nl(s, i);
*/
static gap skip_gap(const sc_arena *a, sc_str s, size_t i) {
    gap g;
    g.nl = false;
    uint8_t b = byte_at(a, s, i);
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop invariant b == tk_at(s, i);
        loop invariant tk_gap(s, i) == tk_gap(s, \at(i, Pre));
        loop invariant tk_gap_nl(s, \at(i, Pre)) <==> (g.nl || tk_gap_nl(s, i));
        loop assigns i, b, g.nl;
        loop variant s.len - i;
    */
    while (is_space(b) || (b == '-' && byte_at(a, s, i + 1) == '-')) {
        if (b == '-') {
            i = run_line(a, s, i + 1);
        } else {
            g.nl = g.nl || b == '\n' || b == '\r';
            i++;
        }
        b = byte_at(a, s, i);
    }
    g.end = i;
    return g;
}

/*@ requires valid_arena(a) && valid_slice(a, s) && tag_ok(st);
    requires start <= i <= s.len && start < s.len;
    assigns \nothing;
    ensures \result == tk_quote(s, start, i, st, at_end);
    ensures sqltok_result(a, s, start, st, \result);
    ensures start < i || at_end ==> \result.kind != SQLTOK_NONE;
*/
static sqltok_token quote_body(const sc_arena *a, sc_str s, size_t start, size_t i,
                               sqltok_state st, bool at_end) {
    uint8_t qc = st.quote == SQLTOK_Q_BRACKET ? ']' : st.quote == SQLTOK_Q_BACKTICK ? '`' :
                 st.quote == SQLTOK_Q_IDENT ? '"' : '\'';
    uint8_t standard = st.dialect == SQLTOK_SQLITE ? 2u :
                       st.std_strings || st.dialect != SQLTOK_POSTGRESQL ? 1u : 0u;
    bool escapes = st.quote == SQLTOK_Q_EXT || (st.quote == SQLTOK_Q_PLAIN && standard == 0);
    uint8_t b = 0;
    /*@ loop invariant start <= i <= s.len && \at(i, Pre) <= i;
        loop invariant tk_qend(s, i, st.quote, standard, at_end) ==
                       tk_qend(s, \at(i, Pre), st.quote, standard, at_end);
        loop assigns i, b;
        loop variant s.len - i;
    */
    while (sc_slice_get(a, s, i, &b)) {
        uint8_t c = 0;
        bool more = sc_slice_get(a, s, i + 1, &c);
        if (b == qc) {
            if (st.quote != SQLTOK_Q_BIT && st.quote != SQLTOK_Q_BRACKET && c == qc) {
                /*@ assert tk_qend(s, i + 2, st.quote, standard, at_end) == tk_qend(s, i, st.quote, standard, at_end); */
                i += 2;
                continue;
            }
            if (st.quote >= SQLTOK_Q_IDENT || standard == 2) {
                if (!more && !at_end) {
                    return open_part(start, i, st);
                }
                return tok(st.quote >= SQLTOK_Q_IDENT ? SQLTOK_QIDENT : SQLTOK_STRING, i + 1, closed(st));
            }
            gap g = skip_gap(a, s, i + 1);
            bool hold = g.end >= s.len || (byte_at(a, s, g.end) == '-' && g.end + 1 >= s.len);
            if (hold && !at_end) {
                return open_part(start, i, st);
            }
            if (!g.nl || byte_at(a, s, g.end) != '\'') {
                return tok(SQLTOK_STRING, i + 1, closed(st));
            }
            /*@ assert tk_qend(s, g.end + 1, st.quote, standard, at_end) == tk_qend(s, i, st.quote, standard, at_end); */
            i = g.end + 1;
            continue;
        }
        if (b == '\\' && escapes) {
            if (!more && !at_end) {
                return open_part(start, i, st);
            }
            if (more) {
                /*@ assert tk_qend(s, i + 2, st.quote, standard, at_end) == tk_qend(s, i, st.quote, standard, at_end); */
                i++;
            } else {
                /*@ assert tk_qend(s, i + 1, st.quote, standard, at_end) == tk_qend(s, i, st.quote, standard, at_end); */
            }
        } else {
            /*@ assert tk_qend(s, i + 1, st.quote, standard, at_end) == tk_qend(s, i, st.quote, standard, at_end); */
        }
        i++;
    }
    return open_end(start, i, st, at_end);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && st.mode == SQLTOK_IN_COMMENT;
    requires start <= i <= s.len && start < s.len;
    assigns \nothing;
    ensures \result == tk_comment(s, start, i, st, at_end);
    ensures sqltok_result(a, s, start, st, \result);
    ensures start < i || at_end ==> \result.kind != SQLTOK_NONE;
*/
static sqltok_token comment_body(const sc_arena *a, sc_str s, size_t start, size_t i,
                                 sqltok_state st, bool at_end) {
    uint8_t b = 0;
    /*@ loop invariant start <= i <= s.len && \at(i, Pre) <= i;
        loop invariant i == start ==> st == \at(st, Pre);
        loop invariant st == { \at(st, Pre) \with .depth = st.depth };
        loop invariant tk_cend(s, i, st.depth, at_end, st.dialect != SQLTOK_SQLITE ? 1 : 0) ==
                       tk_cend(s, \at(i, Pre), \at(st, Pre).depth, at_end, st.dialect != SQLTOK_SQLITE ? 1 : 0);
        loop invariant tk_cdepth(s, i, st.depth, at_end, st.dialect != SQLTOK_SQLITE ? 1 : 0) ==
                       tk_cdepth(s, \at(i, Pre), \at(st, Pre).depth, at_end, st.dialect != SQLTOK_SQLITE ? 1 : 0);
        loop assigns i, b, st.depth;
        loop variant s.len - i;
    */
    while (sc_slice_get(a, s, i, &b)) {
        uint8_t c = 0;
        bool more = sc_slice_get(a, s, i + 1, &c);
        if ((b == '*' || b == '/') && !more && !at_end) {
            return open_part(start, i, st);
        }
        if (b == '*' && c == '/') {
            if (st.depth <= 1) {
                return tok(SQLTOK_BLOCK_COMMENT, i + 2, closed(st));
            }
            st.depth--;
            i += 2;
            continue;
        }
        if (b == '/' && c == '*' && st.dialect != SQLTOK_SQLITE) {
            if (st.depth < SIZE_MAX) {
                st.depth++;
            }
            i += 2;
            continue;
        }
        i++;
    }
    return open_end(start, i, st, at_end);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && i < s.len;
    requires tag.len == 0 || (valid_arena(ta) && valid_slice(ta, tag));
    assigns \nothing;
    ensures \result <==> tk_closes(s, i, tag);
*/
static bool closes_at(const sc_arena *a, sc_str s, size_t i, const sc_arena *ta, sc_str tag) {
    /*@ loop invariant 0 <= k <= tag.len;
        loop invariant \forall integer m; 0 <= m < k ==> tk_at(s, i + 1 + m) == tag.ptr[m];
        loop assigns k;
        loop variant tag.len - k;
    */
    for (size_t k = 0; k < tag.len; k++) {
        uint8_t x = 0;
        uint8_t y = 0;
        if (!sc_slice_get(a, s, i + 1 + k, &x) || !sc_slice_get(ta, tag, k, &y) || x != y) {
            return false;
        }
    }
    return byte_at(a, s, i + 1 + tag.len) == '$';
}

/*@ requires valid_arena(a) && valid_slice(a, s) && tag_ok(st) && st.mode == SQLTOK_IN_DOLLAR;
    requires start <= i <= s.len && start < s.len;
    assigns \nothing;
    ensures \result == tk_dollar(s, start, i, st, at_end);
    ensures sqltok_result(a, s, start, st, \result);
    ensures (start < i || at_end) && !(st.tag.len > 0 && !valid_slice(st.tag_arena, st.tag)) ==>
            \result.kind != SQLTOK_NONE;
*/
static sqltok_token dollar_body(const sc_arena *a, sc_str s, size_t start, size_t i,
                                sqltok_state st, bool at_end) {
    uint8_t b = 0;
    if (st.tag.len > 0 && !sc_slice_get(st.tag_arena, st.tag, 0, &b)) {
        return tok(SQLTOK_NONE, start, st);
    }
    /*@ loop invariant start <= i <= s.len && \at(i, Pre) <= i;
        loop invariant tk_dend(s, i, st.tag, at_end) == tk_dend(s, \at(i, Pre), st.tag, at_end);
        loop assigns i, b;
        loop variant s.len - i;
    */
    while (sc_slice_get(a, s, i, &b)) {
        if (!at_end && s.len - i - 1 <= st.tag.len) {
            return open_part(start, i, st);
        }
        if (b == '$' && closes_at(a, s, i, st.tag_arena, st.tag)) {
            return tok(SQLTOK_DOLLAR, i + st.tag.len + 2, closed(st));
        }
        i++;
    }
    return open_end(start, i, st, at_end);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && off < s.len;
    requires tk_digit(s.ptr[off]) || s.ptr[off] == '.';
    assigns \nothing;
    ensures off < \result <= s.len && \result == tk_frac(s, off);
*/
static size_t number_frac(const sc_arena *a, sc_str s, size_t off) {
    if (byte_at(a, s, off) == '.') {
        return run_digits(a, s, off + 1);
    }
    size_t i = run_digits(a, s, off);
    if (byte_at(a, s, i) == '.' && byte_at(a, s, i + 1) != '.') {
        return run_digits(a, s, i + 1);
    }
    return i;
}

/*@ requires valid_arena(a) && valid_slice(a, s) && i <= s.len && !tk_early(s, i);
    assigns \nothing;
    ensures i <= \result <= s.len && \result == tk_exp_end(s, i);
*/
static size_t number_exp(const sc_arena *a, sc_str s, size_t i) {
    uint8_t e = byte_at(a, s, i);
    if (e != 'e' && e != 'E') {
        return i;
    }
    uint8_t c = byte_at(a, s, i + 1);
    if (is_digit(c)) {
        return run_digits(a, s, i + 1);
    }
    if (c == '+' || c == '-') {
        return run_digits(a, s, i + 2);
    }
    return i;
}

/*@ requires valid_arena(a) && valid_slice(a, s) && off < i <= s.len;
    assigns \nothing;
    ensures i <= \result <= s.len && \result == tk_tail(s, off, i);
*/
static size_t number_tail(const sc_arena *a, sc_str s, size_t off, size_t i) {
    if (ident_start(byte_at(a, s, i))) {
        i = run_ident(a, s, i + 1);
    }
    size_t end = i;
    size_t q = off + 1;
    /*@ loop invariant off + 1 <= q <= s.len && i <= end <= s.len;
        loop invariant tk_junk(s, off, q, i, end) == tk_junk(s, off, off + 1, i, i);
        loop assigns q, end;
        loop variant s.len - q;
    */
    while (q < i) {
        uint8_t prev = byte_at(a, s, q - 1);
        if (ident_start(byte_at(a, s, q)) && (is_digit(prev) || (prev == '.' && q - 1 > off))) {
            q = run_ident(a, s, q + 1);
            if (q > end) {
                end = q;
            }
        } else {
            q++;
        }
    }
    return end;
}

/*@ requires valid_arena(a) && valid_slice(a, s) && off < s.len;
    requires tk_digit(s.ptr[off]) || s.ptr[off] == '.';
    assigns \nothing;
    ensures off < \result <= s.len && \result == tk_number(s, off);
*/
static size_t run_number(const sc_arena *a, sc_str s, size_t off) {
    size_t i = number_frac(a, s, off);
    uint8_t e = byte_at(a, s, i);
    uint8_t c = byte_at(a, s, i + 1);
    if ((e == 'e' || e == 'E') && (c == '+' || c == '-') && !is_digit(byte_at(a, s, i + 2))) {
        return i + 2;
    }
    return number_tail(a, s, off, number_exp(a, s, i));
}

/*@ requires valid_arena(a) && valid_slice(a, s) && tag_ok(st);
    requires off < body <= s.len;
    assigns \nothing;
    ensures \result == tk_quote(s, off, body, tk_in_quote(st, quote), at_end);
    ensures sqltok_result(a, s, off, st, \result) && \result.kind != SQLTOK_NONE;
*/
static sqltok_token open_quote(const sc_arena *a, sc_str s, size_t off, size_t body,
                               sqltok_state st, uint8_t quote, bool at_end) {
    st.mode = SQLTOK_IN_QUOTE;
    st.quote = quote;
    return quote_body(a, s, off, body, st, at_end);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && tag_ok(st) && off < s.len;
    requires st.mode != SQLTOK_IN_DOLLAR;
    assigns \nothing;
    ensures \result == tk_lex_dollar(a, s, off, st, at_end);
    ensures sqltok_result(a, s, off, st, \result);
    ensures at_end ==> \result.kind != SQLTOK_NONE;
*/
static sqltok_token lex_dollar(const sc_arena *a, sc_str s, size_t off, sqltok_state st,
                               bool at_end) {
    uint8_t b = byte_at(a, s, off + 1);
    if (is_digit(b)) {
        size_t i = run_plain_digits(a, s, off + 2);
        if (ident_start(byte_at(a, s, i))) {
            i = run_ident(a, s, i + 1);
        }
        return simple(SQLTOK_PARAM, off, i, i, s.len, st, at_end);
    }
    size_t t = ident_start(b) ? run_tag(a, s, off + 2) : off + 1;
    if (byte_at(a, s, t) != '$') {
        return simple(SQLTOK_OP, off, off + 1, t, s.len, st, at_end);
    }
    st.mode = SQLTOK_IN_DOLLAR;
    st.tag = sc_slice_sub(a, s, off + 1, t - off - 1);
    /*@ assert st.tag == tk_tag(s, off, t); */
    st.tag_arena = a;
    return dollar_body(a, s, off, t + 1, st, at_end);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && tag_ok(st) && off < s.len;
    requires b == s.ptr[off];
    assigns \nothing;
    ensures \result == tk_lex_word(s, off, st, at_end);
    ensures sqltok_result(a, s, off, st, \result);
    ensures at_end ==> \result.kind != SQLTOK_NONE;
*/
static sqltok_token lex_word(const sc_arena *a, sc_str s, size_t off, uint8_t b, sqltok_state st,
                             bool at_end) {
    uint8_t c = byte_at(a, s, off + 1);
    if (c == '\'') {
        if (st.dialect != SQLTOK_SQLITE && (b == 'e' || b == 'E')) {
            return open_quote(a, s, off, off + 2, st, SQLTOK_Q_EXT, at_end);
        }
        if (st.dialect == SQLTOK_POSTGRESQL && (b == 'b' || b == 'B' || b == 'x' || b == 'X')) {
            return open_quote(a, s, off, off + 2, st, SQLTOK_Q_BIT, at_end);
        }
        if (st.dialect == SQLTOK_POSTGRESQL && (b == 'n' || b == 'N')) {
            return open_quote(a, s, off, off + 2, st, SQLTOK_Q_PLAIN, at_end);
        }
    }
    if (st.dialect == SQLTOK_POSTGRESQL && (b == 'u' || b == 'U') && c == '&') {
        uint8_t d = byte_at(a, s, off + 2);
        if (d == '\'') {
            return open_quote(a, s, off, off + 3, st, SQLTOK_Q_UNI, at_end);
        }
        if (d == '"') {
            return open_quote(a, s, off, off + 3, st, SQLTOK_Q_IDENT, at_end);
        }
        return simple(SQLTOK_OP, off, off + 1, off + 2, s.len, st, at_end);
    }
    size_t end = run_ident(a, s, off + 1);
    return simple(SQLTOK_WORD, off, end, end, s.len, st, at_end);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && tag_ok(st) && off < s.len;
    requires st.mode != SQLTOK_IN_DOLLAR;
    requires b == s.ptr[off] && c == tk_at(s, off + 1);
    requires !tk_space(b) && !(b == '-' && c == '-') && !(b == '/' && c == '*') && b != '\'' && b != '"';
    requires !(st.dialect != SQLTOK_POSTGRESQL && b == '`') && !(st.dialect == SQLTOK_SQLITE && b == '[');
    assigns \nothing;
    ensures \result == tk_lex(a, s, off, st, at_end);
    ensures sqltok_result(a, s, off, st, \result);
    ensures at_end ==> \result.kind != SQLTOK_NONE;
*/
static sqltok_token lex_rest(const sc_arena *a, sc_str s, size_t off, uint8_t b, uint8_t c,
                             sqltok_state st, bool at_end) {
    if (st.dialect == SQLTOK_SQLITE && (b == '$' || b == '@' || b == ':' || b == '#')) {
        size_t end = run_parameter(a, s, off + 1);
        size_t look = byte_at(a, s, end) == ':' ? end + 1u : end;
        return simple(SQLTOK_PARAM, off, end, look, s.len, st, at_end);
    }
    if (st.dialect == SQLTOK_SQLITE && b == '?') {
        size_t end = run_plain_digits(a, s, off + 1);
        return simple(SQLTOK_PARAM, off, end, end, s.len, st, at_end);
    }
    if (b == '$' && st.dialect != SQLTOK_SQLITE) {
        return lex_dollar(a, s, off, st, at_end);
    }
    if (is_digit(b) || (b == '.' && is_digit(c))) {
        size_t end = run_number(a, s, off);
        return simple(SQLTOK_NUMBER, off, end, end, s.len, st, at_end);
    }
    if (ident_start(b)) {
        return lex_word(a, s, off, b, st, at_end);
    }
    size_t look = b == '-' || b == '/' || b == '.' ? off + 1 : off;
    return simple(SQLTOK_OP, off, off + 1, look, s.len, st, at_end);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && tag_ok(st) && off < s.len;
    requires st.mode != SQLTOK_IN_DOLLAR;
    requires b == s.ptr[off];
    assigns \nothing;
    ensures \result == tk_lex(a, s, off, st, at_end);
    ensures sqltok_result(a, s, off, st, \result);
    ensures at_end ==> \result.kind != SQLTOK_NONE;
*/
static sqltok_token lex_none(const sc_arena *a, sc_str s, size_t off, uint8_t b, sqltok_state st,
                             bool at_end) {
    if (is_space(b)) {
        size_t end = run_space(a, s, off + 1);
        return simple(SQLTOK_SPACE, off, end, end, s.len, st, at_end);
    }
    uint8_t c = byte_at(a, s, off + 1);
    if (b == '-' && c == '-') {
        size_t end = run_line(a, s, off + 2);
        return simple(SQLTOK_LINE_COMMENT, off, end, end, s.len, st, at_end);
    }
    if (b == '/' && c == '*') {
        st.mode = SQLTOK_IN_COMMENT;
        st.depth = 1;
        return comment_body(a, s, off, off + 2, st, at_end);
    }
    if (b == '\'') {
        return open_quote(a, s, off, off + 1, st, SQLTOK_Q_PLAIN, at_end);
    }
    if (b == '"') {
        return open_quote(a, s, off, off + 1, st, SQLTOK_Q_IDENT, at_end);
    }
    if (st.dialect != SQLTOK_POSTGRESQL && b == '`') {
        return open_quote(a, s, off, off + 1, st, SQLTOK_Q_BACKTICK, at_end);
    }
    if (st.dialect == SQLTOK_SQLITE && b == '[') {
        return open_quote(a, s, off, off + 1, st, SQLTOK_Q_BRACKET, at_end);
    }
    return lex_rest(a, s, off, b, c, st, at_end);
}

sqltok_token sqltok_step(const sc_arena *a, sc_str s, size_t off, sqltok_state st, bool at_end) {
    uint8_t b = 0;
    if (!sc_slice_get(a, s, off, &b)) {
        return tok(SQLTOK_NONE, off, st);
    }
    if (st.mode == SQLTOK_IN_QUOTE) {
        return quote_body(a, s, off, off, st, at_end);
    }
    if (st.mode == SQLTOK_IN_COMMENT) {
        return comment_body(a, s, off, off, st, at_end);
    }
    if (st.mode == SQLTOK_IN_DOLLAR) {
        return dollar_body(a, s, off, off, st, at_end);
    }
    return lex_none(a, s, off, b, st, at_end);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && 'a' <= want <= 'z';
    assigns \nothing;
    ensures \result <==> tk_ci(s, i, want);
*/
static bool letter_at(const sc_arena *a, sc_str s, size_t i, uint8_t want) {
    uint8_t b = 0;
    return sc_slice_get(a, s, i, &b) && (b == want || b == (uint8_t)(want - 0x20u));
}

/*@ requires valid_arena(a) && valid_slice(a, s) && off <= s.len && n <= s.len - off;
    requires 'a' <= c0 <= 'z' && 'a' <= c1 <= 'z';
    assigns \nothing;
    ensures \result <==> tk_w2(s, off, n, c0, c1);
*/
static bool word2(const sc_arena *a, sc_str s, size_t off, size_t n, uint8_t c0, uint8_t c1) {
    return n == 2 &&
           letter_at(a, s, off, c0) &&
           letter_at(a, s, off + 1u, c1);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && off <= s.len && n <= s.len - off;
    requires 'a' <= c0 <= 'z' && 'a' <= c1 <= 'z' && 'a' <= c2 <= 'z';
    assigns \nothing;
    ensures \result <==> tk_w3(s, off, n, c0, c1, c2);
*/
static bool word3(const sc_arena *a, sc_str s, size_t off, size_t n, uint8_t c0, uint8_t c1, uint8_t c2) {
    return n == 3 &&
           letter_at(a, s, off, c0) &&
           letter_at(a, s, off + 1u, c1) &&
           letter_at(a, s, off + 2u, c2);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && off <= s.len && n <= s.len - off;
    requires 'a' <= c0 <= 'z' && 'a' <= c1 <= 'z' && 'a' <= c2 <= 'z' && 'a' <= c3 <= 'z';
    assigns \nothing;
    ensures \result <==> tk_w4(s, off, n, c0, c1, c2, c3);
*/
static bool word4(const sc_arena *a, sc_str s, size_t off, size_t n, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3) {
    return n == 4 &&
           letter_at(a, s, off, c0) &&
           letter_at(a, s, off + 1u, c1) &&
           letter_at(a, s, off + 2u, c2) &&
           letter_at(a, s, off + 3u, c3);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && off <= s.len && n <= s.len - off;
    requires 'a' <= c0 <= 'z' && 'a' <= c1 <= 'z' && 'a' <= c2 <= 'z' && 'a' <= c3 <= 'z' && 'a' <= c4 <= 'z';
    assigns \nothing;
    ensures \result <==> tk_w5(s, off, n, c0, c1, c2, c3, c4);
*/
static bool word5(const sc_arena *a, sc_str s, size_t off, size_t n, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4) {
    return n == 5 &&
           letter_at(a, s, off, c0) &&
           letter_at(a, s, off + 1u, c1) &&
           letter_at(a, s, off + 2u, c2) &&
           letter_at(a, s, off + 3u, c3) &&
           letter_at(a, s, off + 4u, c4);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && off <= s.len && n <= s.len - off;
    requires 'a' <= c0 <= 'z' && 'a' <= c1 <= 'z' && 'a' <= c2 <= 'z' && 'a' <= c3 <= 'z' && 'a' <= c4 <= 'z' && 'a' <= c5 <= 'z';
    assigns \nothing;
    ensures \result <==> tk_w6(s, off, n, c0, c1, c2, c3, c4, c5);
*/
static bool word6(const sc_arena *a, sc_str s, size_t off, size_t n, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, uint8_t c5) {
    return n == 6 &&
           letter_at(a, s, off, c0) &&
           letter_at(a, s, off + 1u, c1) &&
           letter_at(a, s, off + 2u, c2) &&
           letter_at(a, s, off + 3u, c3) &&
           letter_at(a, s, off + 4u, c4) &&
           letter_at(a, s, off + 5u, c5);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && off <= s.len && n <= s.len - off;
    requires 'a' <= c0 <= 'z' && 'a' <= c1 <= 'z' && 'a' <= c2 <= 'z' && 'a' <= c3 <= 'z' && 'a' <= c4 <= 'z' && 'a' <= c5 <= 'z' && 'a' <= c6 <= 'z';
    assigns \nothing;
    ensures \result <==> tk_w7(s, off, n, c0, c1, c2, c3, c4, c5, c6);
*/
static bool word7(const sc_arena *a, sc_str s, size_t off, size_t n, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, uint8_t c5, uint8_t c6) {
    return n == 7 &&
           letter_at(a, s, off, c0) &&
           letter_at(a, s, off + 1u, c1) &&
           letter_at(a, s, off + 2u, c2) &&
           letter_at(a, s, off + 3u, c3) &&
           letter_at(a, s, off + 4u, c4) &&
           letter_at(a, s, off + 5u, c5) &&
           letter_at(a, s, off + 6u, c6);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && off <= s.len && n <= s.len - off;
    requires 'a' <= c0 <= 'z' && 'a' <= c1 <= 'z' && 'a' <= c2 <= 'z' && 'a' <= c3 <= 'z' && 'a' <= c4 <= 'z' && 'a' <= c5 <= 'z' && 'a' <= c6 <= 'z' && 'a' <= c7 <= 'z';
    assigns \nothing;
    ensures \result <==> tk_w8(s, off, n, c0, c1, c2, c3, c4, c5, c6, c7);
*/
static bool word8(const sc_arena *a, sc_str s, size_t off, size_t n, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, uint8_t c5, uint8_t c6, uint8_t c7) {
    return n == 8 &&
           letter_at(a, s, off, c0) &&
           letter_at(a, s, off + 1u, c1) &&
           letter_at(a, s, off + 2u, c2) &&
           letter_at(a, s, off + 3u, c3) &&
           letter_at(a, s, off + 4u, c4) &&
           letter_at(a, s, off + 5u, c5) &&
           letter_at(a, s, off + 6u, c6) &&
           letter_at(a, s, off + 7u, c7);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && off <= s.len && n <= s.len - off;
    requires 'a' <= c0 <= 'z' && 'a' <= c1 <= 'z' && 'a' <= c2 <= 'z' && 'a' <= c3 <= 'z' && 'a' <= c4 <= 'z' && 'a' <= c5 <= 'z' && 'a' <= c6 <= 'z' && 'a' <= c7 <= 'z' && 'a' <= c8 <= 'z';
    assigns \nothing;
    ensures \result <==> tk_w9(s, off, n, c0, c1, c2, c3, c4, c5, c6, c7, c8);
*/
static bool word9(const sc_arena *a, sc_str s, size_t off, size_t n, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, uint8_t c5, uint8_t c6, uint8_t c7, uint8_t c8) {
    return n == 9 &&
           letter_at(a, s, off, c0) &&
           letter_at(a, s, off + 1u, c1) &&
           letter_at(a, s, off + 2u, c2) &&
           letter_at(a, s, off + 3u, c3) &&
           letter_at(a, s, off + 4u, c4) &&
           letter_at(a, s, off + 5u, c5) &&
           letter_at(a, s, off + 6u, c6) &&
           letter_at(a, s, off + 7u, c7) &&
           letter_at(a, s, off + 8u, c8);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && off <= s.len && n <= s.len - off;
    assigns \nothing;
    ensures \result == tk_word(s, off, n, dialect);
*/
static uint8_t word_kind(const sc_arena *a, sc_str s, size_t off, size_t n, uint8_t dialect) {
    if (dialect == SQLTOK_SQLITE && word7(a, s, off, n, 'e', 'x', 'p', 'l', 'a', 'i', 'n')) {
        return SQLTOK_W_EXPLAIN;
    }
    if (dialect == SQLTOK_SQLITE && word5(a, s, off, n, 'q', 'u', 'e', 'r', 'y')) {
        return SQLTOK_W_QUERY;
    }
    if (dialect == SQLTOK_SQLITE && word4(a, s, off, n, 'p', 'l', 'a', 'n')) {
        return SQLTOK_W_PLAN;
    }
    if (dialect == SQLTOK_SQLITE && word7(a, s, off, n, 't', 'r', 'i', 'g', 'g', 'e', 'r')) {
        return SQLTOK_W_ROUTINE;
    }
    if (dialect == SQLTOK_SQLITE && (word4(a, s, off, n, 't', 'e', 'm', 'p') ||
        word9(a, s, off, n, 't', 'e', 'm', 'p', 'o', 'r', 'a', 'r', 'y'))) {
        return SQLTOK_W_TEMP;
    }
    if (word2(a, s, off, n, 'o', 'r')) {
        return SQLTOK_W_OR;
    }
    if (word3(a, s, off, n, 'e', 'n', 'd')) {
        return SQLTOK_W_END;
    }
    if (word4(a, s, off, n, 'c', 'a', 's', 'e')) {
        return SQLTOK_W_CASE;
    }
    if (word5(a, s, off, n, 'b', 'e', 'g', 'i', 'n')) {
        return SQLTOK_W_BEGIN;
    }
    if (word6(a, s, off, n, 'c', 'r', 'e', 'a', 't', 'e')) {
        return SQLTOK_W_CREATE;
    }
    if (word7(a, s, off, n, 'r', 'e', 'p', 'l', 'a', 'c', 'e')) {
        return SQLTOK_W_REPLACE;
    }
    if (dialect != SQLTOK_SQLITE && word8(a, s, off, n, 'f', 'u', 'n', 'c', 't', 'i', 'o', 'n')) {
        return SQLTOK_W_ROUTINE;
    }
    if (dialect != SQLTOK_SQLITE && word9(a, s, off, n, 'p', 'r', 'o', 'c', 'e', 'd', 'u', 'r', 'e')) {
        return SQLTOK_W_ROUTINE;
    }
    return SQLTOK_W_OTHER;
}

/*@ assigns \nothing;
    ensures \result == tk_prefix(p, w);
*/
static uint8_t prefix_next(uint8_t p, uint8_t w) {
    if (p == SQLTOK_P_START && w == SQLTOK_W_EXPLAIN) {
        return SQLTOK_P_EXPLAIN;
    }
    if (p == SQLTOK_P_EXPLAIN && w == SQLTOK_W_QUERY) {
        return SQLTOK_P_QUERY;
    }
    if (p == SQLTOK_P_QUERY && w == SQLTOK_W_PLAN) {
        return SQLTOK_P_PLAN;
    }
    if ((p == SQLTOK_P_START || p == SQLTOK_P_EXPLAIN || p == SQLTOK_P_PLAN) && w == SQLTOK_W_CREATE) {
        return SQLTOK_P_CREATE;
    }
    if (p == SQLTOK_P_CREATE && w == SQLTOK_W_TEMP) {
        return SQLTOK_P_CREATE;
    }
    if (p == SQLTOK_P_CREATE && w == SQLTOK_W_OR) {
        return SQLTOK_P_OR;
    }
    if (p == SQLTOK_P_OR && w == SQLTOK_W_REPLACE) {
        return SQLTOK_P_REPLACE;
    }
    if ((p == SQLTOK_P_CREATE || p == SQLTOK_P_REPLACE) && w == SQLTOK_W_ROUTINE) {
        return SQLTOK_P_ROUTINE;
    }
    return p == SQLTOK_P_ROUTINE ? SQLTOK_P_ROUTINE : SQLTOK_P_NO;
}

/*@ assigns \nothing;
    ensures \result == tk_begin(depth, w);
*/
static size_t begin_next(size_t depth, uint8_t w) {
    if (w == SQLTOK_W_BEGIN && depth < SIZE_MAX) {
        return depth + 1;
    }
    if (w == SQLTOK_W_CASE && depth >= 1 && depth < SIZE_MAX) {
        return depth + 1;
    }
    if (w == SQLTOK_W_END && depth > 0) {
        return depth - 1;
    }
    return depth;
}

/*@ assigns \nothing;
    ensures \result == tk_trigger(depth, kind, word, byte);
*/
static size_t trigger_next(size_t depth, uint8_t kind, uint8_t word, uint8_t byte) {
    if (kind == SQLTOK_SPACE || kind == SQLTOK_LINE_COMMENT || kind == SQLTOK_BLOCK_COMMENT) {
        return depth;
    }
    if (kind == SQLTOK_OP && byte == ';') {
        return 2;
    }
    return kind == SQLTOK_WORD && word == SQLTOK_W_END && depth == 2 ? 3u : 1u;
}

size_t sqltok_stmt_end(const sc_arena *a, sc_str s, size_t from, bool std, uint8_t dialect) {
    sqltok_state st;
    st.mode = SQLTOK_IN_NONE;
    st.depth = 0;
    st.quote = SQLTOK_Q_PLAIN;
    st.tag = sc_slice_sub(a, s, 0, 0);
    st.tag_arena = a;
    st.std_strings = std;
    st.dialect = dialect;
    size_t paren = 0;
    size_t begin = 0;
    uint8_t prefix = SQLTOK_P_START;
    size_t off = from;
    /*@ loop invariant off == from || from < off <= s.len;
        loop invariant tag_ok(st);
        loop invariant tk_walk(a, s, off, tk_step(a, s, off, st, 1), paren, begin, prefix) ==
                       tk_split(a, s, from, std, dialect);
        loop assigns off, st, paren, begin, prefix;
        loop variant s.len - off;
    */
    for (;;) {
        sqltok_token t = sqltok_step(a, s, off, st, true);
        if (t.kind == SQLTOK_NONE) {
            return s.len;
        }
        uint8_t b = byte_at(a, s, off);
        uint8_t w = SQLTOK_W_OTHER;
        if (t.kind == SQLTOK_OP) {
            if (b == ';' && paren == 0 && (begin == 0 ||
                (st.dialect == SQLTOK_SQLITE && prefix == SQLTOK_P_ROUTINE && begin == 3))) {
                return off;
            }
            if (b == '(' && paren < SIZE_MAX) {
                paren++;
            }
            if (b == ')' && paren > 0) {
                paren--;
            }
        }
        if (t.kind == SQLTOK_WORD && paren == 0) {
            w = word_kind(a, s, off, t.end - off, st.dialect);
            prefix = prefix_next(prefix, w);
            if (prefix == SQLTOK_P_ROUTINE && st.dialect != SQLTOK_SQLITE) {
                begin = begin_next(begin, w);
            }
        }
        if (st.dialect == SQLTOK_SQLITE && prefix == SQLTOK_P_ROUTINE) {
            begin = trigger_next(begin, t.kind, w, b);
        }
        off = t.end;
        st = t.next;
    }
}
