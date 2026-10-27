/* The editor's SQL tokenizer and statement splitter (plan U14). See
 * sqltok.h. Every read goes through sc_slice_get, so no byte outside the
 * slice is ever read. */
#include "sqltok.h"

/* The byte at `i`, or 0 with *past set when `i` is outside `s`. A token whose
 * lexing set *past might grow with more input. */
/*@ predicate past_ok(sc_arena *a, bool *past) =
      \valid(past) && \separated(past, a, a->base + (0 .. a->cap - 1));
*/

/*@ requires valid_arena(a) && valid_slice(a, s) && past_ok(a, past);
    assigns *past;
    ensures i < s.len ==> \result == s.ptr[i];
    ensures i >= s.len ==> \result == 0;
*/
static uint8_t peek(const sc_arena *a, sc_str s, size_t i, bool *past) {
    uint8_t b = 0;
    if (!sc_slice_get(a, s, i, &b)) {
        *past = true;
        return 0;
    }
    return b;
}

/*@ assigns \nothing;
    ensures \result <==> lo <= b <= hi;
*/
static bool between(uint8_t b, uint8_t lo, uint8_t hi) {
    return b >= lo && b <= hi;
}

/*@ assigns \nothing;
    ensures \result ==> b != 0;
*/
static bool is_space(uint8_t b) {
    return b == ' ' || b == '\t' || b == '\n' || b == '\r' || b == '\f' || b == '\v';
}

/*@ assigns \nothing;
    ensures \result <==> '0' <= b <= '9';
*/
static bool is_digit(uint8_t b) {
    return between(b, '0', '9');
}

/* psql's ident_start: a letter, `_` or any byte of a multibyte character. */
/*@ assigns \nothing;
    ensures \result ==> b != 0;
*/
static bool ident_start(uint8_t b) {
    return between(b, 'a', 'z') || between(b, 'A', 'Z') || b == '_' || b >= 0x80;
}

/* psql's dolq_cont: a dollar tag's later bytes. */
/*@ assigns \nothing;
    ensures \result ==> b != 0;
*/
static bool dolq_cont(uint8_t b) {
    return ident_start(b) || is_digit(b);
}

/* psql's ident_cont: an identifier's later bytes. */
/*@ assigns \nothing;
    ensures \result ==> b != 0;
*/
static bool ident_cont(uint8_t b) {
    return dolq_cont(b) || b == '$';
}

/*@ assigns \nothing;
    ensures \result.kind == SQLTOK_NONE && \result.end == off && \result.next == st;
*/
static sqltok_token none(size_t off, sqltok_state st) {
    sqltok_token t;
    t.kind = SQLTOK_NONE;
    t.end = off;
    t.next = st;
    return t;
}

/*@ assigns \nothing;
    ensures \result.kind == kind && \result.end == end && \result.next == next;
*/
static sqltok_token tok(uint8_t kind, size_t end, sqltok_state next) {
    sqltok_token t;
    t.kind = kind;
    t.end = end;
    t.next = next;
    return t;
}

/* `st` with nothing open. */
/*@ requires valid_arena(a) && slice_storage(a, s);
    assigns \nothing;
    ensures slice_storage(a, \result.tag);
*/
static sqltok_state closed(const sc_arena *a, sc_str s, sqltok_state st) {
    st.mode = SQLTOK_IN_NONE;
    st.depth = 0;
    st.quote = SQLTOK_Q_PLAIN;
    st.tag = sc_slice_sub(a, s, 0, 0);
    return st;
}

/* The kind of a construct open in `st`. */
/*@ assigns \nothing;
    ensures \result != SQLTOK_NONE;
*/
static uint8_t open_kind(sqltok_state st) {
    if (st.mode == SQLTOK_IN_COMMENT) {
        return SQLTOK_BLOCK_COMMENT;
    }
    if (st.mode == SQLTOK_IN_DOLLAR) {
        return SQLTOK_DOLLAR;
    }
    return st.quote == SQLTOK_Q_IDENT ? SQLTOK_QIDENT : SQLTOK_STRING;
}

/* A construct that began at `start` and is still open at `stop`, where more
 * input decides what follows. */
/*@ requires start <= stop;
    assigns \nothing;
    ensures \result.end == stop && \result.next == st;
    ensures \result.kind == SQLTOK_NONE <==> stop == start;
*/
static sqltok_token open_part(size_t start, size_t stop, sqltok_state st) {
    if (stop == start) {
        return none(start, st);
    }
    return tok(open_kind(st), stop, st);
}

/* A construct still open at the end of `s` (`i` == s.len). */
/*@ requires start < i;
    assigns \nothing;
    ensures \result.end == i && \result.next == st && \result.kind != SQLTOK_NONE;
*/
static sqltok_token open_end(size_t start, size_t i, sqltok_state st, bool at_end) {
    if (at_end) {
        return tok(SQLTOK_UNTERMINATED, i, st);
    }
    return open_part(start, i, st);
}

/* A line comment's end: the next newline or carriage return. */
/*@ requires valid_arena(a) && valid_slice(a, s) && past_ok(a, past) && i <= s.len;
    assigns *past;
    ensures i <= \result <= s.len;
*/
static size_t run_line(const sc_arena *a, sc_str s, size_t i, bool *past) {
    uint8_t b = 0;
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop assigns i, b;
        loop variant s.len - i;
    */
    while (sc_slice_get(a, s, i, &b) && b != '\n' && b != '\r') {
        i++;
    }
    if (i >= s.len) {
        *past = true;
    }
    return i;
}

/* Where a string closed just before `i` goes on, as psql's quotecontinue:
 * after spaces and line comments that hold a newline, a quote. The quote's
 * offset, or s.len for none; *past is set when more input could decide. */
/*@ requires valid_arena(a) && valid_slice(a, s) && past_ok(a, past) && i <= s.len;
    assigns *past;
    ensures \result == s.len || (i <= \result < s.len && s.ptr[\result] == '\'');
*/
static size_t continue_at(const sc_arena *a, sc_str s, size_t i, bool *past) {
    bool newline = false;
    uint8_t b = peek(a, s, i, past);
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop invariant i < s.len ==> b == s.ptr[i];
        loop invariant i >= s.len ==> b == 0;
        loop assigns i, b, newline, *past;
        loop variant s.len - i;
    */
    while (is_space(b) || (b == '-' && peek(a, s, i + 1, past) == '-')) {
        if (b == '-') {
            i = run_line(a, s, i + 1, past);
        } else {
            newline = newline || b == '\n' || b == '\r';
            i++;
        }
        b = peek(a, s, i, past);
    }
    if (newline && b == '\'') {
        return i;
    }
    return s.len;
}

/* The body of a quoted string or identifier from `i`, for a token that began
 * at `start`. A doubled quote stays inside, except in a bit or hex string.
 * Backslash escapes the next byte in E'' and, with std_strings off, in ''.
 * A string goes on after a closing quote where continue_at finds one. */
/*@ requires valid_arena(a) && valid_slice(a, s) && slice_storage(a, st.tag);
    requires start <= i <= s.len && start < s.len;
    assigns \nothing;
    ensures sqltok_result(a, s, start, st, \result);
    ensures start < i ==> \result.kind != SQLTOK_NONE;
*/
static sqltok_token quote_body(const sc_arena *a, sc_str s, size_t start, size_t i,
                               sqltok_state st, bool at_end) {
    uint8_t qc = st.quote == SQLTOK_Q_IDENT ? '"' : '\'';
    bool plain = st.quote == SQLTOK_Q_PLAIN;
    bool escapes = st.quote == SQLTOK_Q_EXT || (plain && !st.std_strings);
    bool doubles = st.quote != SQLTOK_Q_BIT;
    uint8_t b = 0;
    /*@ loop invariant start <= i <= s.len && \at(i, Pre) <= i;
        loop assigns i, b;
        loop variant s.len - i;
    */
    while (sc_slice_get(a, s, i, &b)) {
        uint8_t c = 0;
        bool more = sc_slice_get(a, s, i + 1, &c);
        if (b == qc) {
            if (more && doubles && c == qc) {
                i += 2;
                continue;
            }
            if (st.quote == SQLTOK_Q_IDENT) {
                if (!more && !at_end) {
                    return open_part(start, i, st);
                }
                return tok(SQLTOK_QIDENT, i + 1, closed(a, s, st));
            }
            bool past = false;
            size_t k = continue_at(a, s, i + 1, &past);
            if (past && !at_end) {
                return open_part(start, i, st);
            }
            if (k == s.len) {
                return tok(SQLTOK_STRING, i + 1, closed(a, s, st));
            }
            i = k + 1;
            continue;
        }
        if (b == '\\' && escapes) {
            if (!more && !at_end) {
                return open_part(start, i, st);
            }
            if (more) {
                i++;
            }
        }
        i++;
    }
    return open_end(start, i, st, at_end);
}

/* The body of a block comment from `i`, nested st.depth deep. */
/*@ requires valid_arena(a) && valid_slice(a, s) && slice_storage(a, st.tag);
    requires start <= i <= s.len && start < s.len;
    assigns \nothing;
    ensures sqltok_result(a, s, start, st, \result);
    ensures start < i ==> \result.kind != SQLTOK_NONE;
*/
static sqltok_token comment_body(const sc_arena *a, sc_str s, size_t start, size_t i,
                                 sqltok_state st, bool at_end) {
    uint8_t b = 0;
    /*@ loop invariant start <= i <= s.len && \at(i, Pre) <= i;
        loop invariant i == start ==> st == \at(st, Pre);
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
                return tok(SQLTOK_BLOCK_COMMENT, i + 2, closed(a, s, st));
            }
            st.depth--;
            i += 2;
            continue;
        }
        if (b == '/' && c == '*') {
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

/* Whether `$tag$` starts at `i` (i < s.len). */
/*@ requires valid_arena(a) && valid_slice(a, s) && slice_storage(a, tag);
    requires i < s.len;
    assigns \nothing;
    ensures \result ==> tag.len < s.len - i - 1;
*/
static bool closes_at(const sc_arena *a, sc_str s, size_t i, sc_str tag) {
    if (s.len - i - 1 <= tag.len) {
        return false;
    }
    /*@ loop invariant 0 <= k <= tag.len;
        loop assigns k;
        loop variant tag.len - k;
    */
    for (size_t k = 0; k < tag.len; k++) {
        uint8_t x = 0;
        uint8_t y = 0;
        if (!sc_slice_get(a, s, i + 1 + k, &x) || !sc_slice_get(a, tag, k, &y) || x != y) {
            return false;
        }
    }
    uint8_t d = 0;
    return sc_slice_get(a, s, i + 1 + tag.len, &d) && d == '$';
}

/* The body of a dollar string from `i`. Without at_end, the last
 * tag.len + 1 bytes are held back: a closing `$tag$` could start there. */
/*@ requires valid_arena(a) && valid_slice(a, s) && slice_storage(a, st.tag);
    requires start <= i <= s.len && start < s.len;
    assigns \nothing;
    ensures sqltok_result(a, s, start, st, \result);
    ensures start < i && (st.tag.len == 0 || valid_slice(a, st.tag)) ==> \result.kind != SQLTOK_NONE;
*/
static sqltok_token dollar_body(const sc_arena *a, sc_str s, size_t start, size_t i,
                                sqltok_state st, bool at_end) {
    uint8_t b = 0;
    if (st.tag.len > 0 && !sc_slice_get(a, st.tag, 0, &b)) {
        return none(start, st);
    }
    /*@ loop invariant start <= i <= s.len && \at(i, Pre) <= i;
        loop assigns i, b;
        loop variant s.len - i;
    */
    while (sc_slice_get(a, s, i, &b)) {
        if (!at_end && s.len - i - 1 <= st.tag.len) {
            return open_part(start, i, st);
        }
        if (b == '$' && closes_at(a, s, i, st.tag)) {
            return tok(SQLTOK_DOLLAR, i + st.tag.len + 2, closed(a, s, st));
        }
        i++;
    }
    return open_end(start, i, st, at_end);
}

/* Runs of space, digits (a `_` only between two digits, as psql's decinteger)
 * and identifier bytes from `i`. */
/*@ requires valid_arena(a) && valid_slice(a, s) && past_ok(a, past) && i <= s.len;
    assigns *past;
    ensures i <= \result <= s.len;
*/
static size_t run_space(const sc_arena *a, sc_str s, size_t i, bool *past) {
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop assigns i, *past;
        loop variant s.len - i;
    */
    while (is_space(peek(a, s, i, past))) {
        i++;
    }
    return i;
}

/*@ requires valid_arena(a) && valid_slice(a, s) && past_ok(a, past) && i <= s.len;
    assigns *past;
    ensures i <= \result <= s.len;
    ensures i < s.len && '0' <= s.ptr[i] <= '9' ==> i < \result;
*/
static size_t run_digits(const sc_arena *a, sc_str s, size_t i, bool *past) {
    uint8_t b = peek(a, s, i, past);
    if (!is_digit(b)) {
        return i;
    }
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop invariant i < s.len ==> b == s.ptr[i];
        loop invariant i >= s.len ==> b == 0;
        loop assigns i, b, *past;
        loop variant s.len - i;
    */
    while (is_digit(b) || (b == '_' && is_digit(peek(a, s, i + 1, past)))) {
        i++;
        b = peek(a, s, i, past);
    }
    return i;
}

/*@ requires valid_arena(a) && valid_slice(a, s) && past_ok(a, past) && i <= s.len;
    assigns *past;
    ensures i <= \result <= s.len;
*/
static size_t run_ident(const sc_arena *a, sc_str s, size_t i, bool *past) {
    /*@ loop invariant \at(i, Pre) <= i <= s.len;
        loop assigns i, *past;
        loop variant s.len - i;
    */
    while (ident_cont(peek(a, s, i, past))) {
        i++;
    }
    return i;
}

/* A number from `off`: digits with `_`, a fraction, an exponent, then any
 * identifier straight after it (psql lexes that trailing junk into the
 * number). `1..` is the integer 1, and `1e+` with no digit stops there.
 * psql takes the longest match, so the junk may also start at a letter or
 * `_` after any digit or after the point: `1e5$` and `1_1.5_5$` are one
 * token each. */
/*@ requires valid_arena(a) && valid_slice(a, s) && past_ok(a, past) && off < s.len;
    requires '0' <= s.ptr[off] <= '9' || s.ptr[off] == '.';
    assigns *past;
    ensures off < \result <= s.len;
*/
static size_t run_number(const sc_arena *a, sc_str s, size_t off, bool *past) {
    size_t i = off;
    if (peek(a, s, i, past) == '.') {
        i = run_digits(a, s, i + 1, past);
    } else {
        i = run_digits(a, s, i, past);
        if (peek(a, s, i, past) == '.' && peek(a, s, i + 1, past) != '.') {
            i = run_digits(a, s, i + 1, past);
        }
    }
    uint8_t e = peek(a, s, i, past);
    if (e == 'e' || e == 'E') {
        uint8_t c = peek(a, s, i + 1, past);
        if (is_digit(c)) {
            i = run_digits(a, s, i + 1, past);
        } else if (c == '+' || c == '-') {
            if (!is_digit(peek(a, s, i + 2, past))) {
                return i + 2;
            }
            i = run_digits(a, s, i + 2, past);
        }
    }
    if (ident_start(peek(a, s, i, past))) {
        i = run_ident(a, s, i + 1, past);
    }
    size_t end = i;
    size_t q = off + 1;
    /*@ loop invariant off + 1 <= q <= s.len && i <= end <= s.len;
        loop assigns q, end, *past;
        loop variant s.len - q;
    */
    while (q < i) {
        uint8_t prev = peek(a, s, q - 1, past);
        if (ident_start(peek(a, s, q, past)) &&
            (is_digit(prev) || (prev == '.' && q - 1 > off))) {
            q = run_ident(a, s, q + 1, past);
            if (q > end) {
                end = q;
            }
        } else {
            q++;
        }
    }
    return end;
}

/* A token that is not a construct: none when it could still grow. */
/*@ requires kind != SQLTOK_NONE;
    assigns \nothing;
    ensures \result.next == st;
    ensures \result.kind == SQLTOK_NONE ==> \result.end == off;
    ensures \result.kind != SQLTOK_NONE ==> \result.end == end;
*/
static sqltok_token simple(uint8_t kind, size_t off, size_t end, sqltok_state st, bool past,
                           bool at_end) {
    if (past && !at_end) {
        return none(off, st);
    }
    return tok(kind, end, st);
}

/*@ requires valid_arena(a) && valid_slice(a, s) && slice_storage(a, st.tag);
    requires off < body <= s.len;
    assigns \nothing;
    ensures sqltok_result(a, s, off, st, \result) && \result.kind != SQLTOK_NONE;
*/
static sqltok_token open_quote(const sc_arena *a, sc_str s, size_t off, size_t body,
                               sqltok_state st, uint8_t quote, bool at_end) {
    st.mode = SQLTOK_IN_QUOTE;
    st.quote = quote;
    return quote_body(a, s, off, body, st, at_end);
}

/* `$` at `off`: a parameter `$1`, a dollar string's opening `$tag$`, or an
 * operator. */
/*@ requires valid_arena(a) && valid_slice(a, s) && slice_storage(a, st.tag) && off < s.len;
    assigns \nothing;
    ensures sqltok_result(a, s, off, st, \result);
*/
static sqltok_token lex_dollar(const sc_arena *a, sc_str s, size_t off, sqltok_state st,
                               bool at_end) {
    bool past = false;
    uint8_t b = peek(a, s, off + 1, &past);
    if (is_digit(b)) {
        size_t i = off + 2;
        /*@ loop invariant off + 2 <= i <= s.len;
            loop assigns i, past;
            loop variant s.len - i;
        */
        while (is_digit(peek(a, s, i, &past))) {
            i++;
        }
        if (ident_start(peek(a, s, i, &past))) {
            i = run_ident(a, s, i + 1, &past);
        }
        return simple(SQLTOK_PARAM, off, i, st, past, at_end);
    }
    size_t i = off + 1;
    if (ident_start(b)) {
        i++;
        /*@ loop invariant off + 1 <= i <= s.len;
            loop assigns i, past;
            loop variant s.len - i;
        */
        while (dolq_cont(peek(a, s, i, &past))) {
            i++;
        }
    }
    if (peek(a, s, i, &past) != '$') {
        return simple(SQLTOK_OP, off, off + 1, st, past, at_end);
    }
    st.mode = SQLTOK_IN_DOLLAR;
    st.tag = sc_slice_sub(a, s, off + 1, i - off - 1);
    return dollar_body(a, s, off, i + 1, st, at_end);
}

/* A letter or other identifier start at `off`: a word, or the prefix of
 * E'', B'', X'', N'', U&'' or U&"". A `u&` with neither quote after it is
 * the operator `u` then `&`. */
/*@ requires valid_arena(a) && valid_slice(a, s) && slice_storage(a, st.tag) && off < s.len;
    assigns \nothing;
    ensures sqltok_result(a, s, off, st, \result);
*/
static sqltok_token lex_word(const sc_arena *a, sc_str s, size_t off, uint8_t b, sqltok_state st,
                             bool at_end) {
    bool past = false;
    uint8_t c = peek(a, s, off + 1, &past);
    if (c == '\'') {
        if (b == 'e' || b == 'E') {
            return open_quote(a, s, off, off + 2, st, SQLTOK_Q_EXT, at_end);
        }
        if (b == 'b' || b == 'B' || b == 'x' || b == 'X') {
            return open_quote(a, s, off, off + 2, st, SQLTOK_Q_BIT, at_end);
        }
        if (b == 'n' || b == 'N') {
            return open_quote(a, s, off, off + 2, st, SQLTOK_Q_PLAIN, at_end);
        }
    }
    if ((b == 'u' || b == 'U') && c == '&') {
        uint8_t d = peek(a, s, off + 2, &past);
        if (d == '\'') {
            return open_quote(a, s, off, off + 3, st, SQLTOK_Q_UNI, at_end);
        }
        if (d == '"') {
            return open_quote(a, s, off, off + 3, st, SQLTOK_Q_IDENT, at_end);
        }
        return simple(SQLTOK_OP, off, off + 1, st, past, at_end);
    }
    size_t end = run_ident(a, s, off + 1, &past);
    return simple(SQLTOK_WORD, off, end, st, past, at_end);
}

/* One token from `off` with nothing open; `b` is the byte at `off`. */
/*@ requires valid_arena(a) && valid_slice(a, s) && slice_storage(a, st.tag) && off < s.len;
    requires b == s.ptr[off];
    assigns \nothing;
    ensures sqltok_result(a, s, off, st, \result);
*/
static sqltok_token lex_none(const sc_arena *a, sc_str s, size_t off, uint8_t b, sqltok_state st,
                             bool at_end) {
    bool past = false;
    if (is_space(b)) {
        size_t end = run_space(a, s, off + 1, &past);
        return simple(SQLTOK_SPACE, off, end, st, past, at_end);
    }
    uint8_t c = peek(a, s, off + 1, &past);
    if (b == '-' && c == '-') {
        size_t end = run_line(a, s, off + 2, &past);
        return simple(SQLTOK_LINE_COMMENT, off, end, st, past, at_end);
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
    if (b == '$') {
        return lex_dollar(a, s, off, st, at_end);
    }
    if (is_digit(b) || (b == '.' && is_digit(c))) {
        size_t end = run_number(a, s, off, &past);
        return simple(SQLTOK_NUMBER, off, end, st, past, at_end);
    }
    if (ident_start(b)) {
        return lex_word(a, s, off, b, st, at_end);
    }
    bool looked = b == '-' || b == '/' || b == '.';
    return simple(SQLTOK_OP, off, off + 1, st, past && looked, at_end);
}

sqltok_token sqltok_step(const sc_arena *a, sc_str s, size_t off, sqltok_state st, bool at_end) {
    uint8_t b = 0;
    if (!sc_slice_get(a, s, off, &b)) {
        return none(off, st);
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

/* Words the splitter tracks, as psql's lexer does. */
#define W_OTHER ((uint8_t)0)
#define W_CREATE ((uint8_t)1)
#define W_OR ((uint8_t)2)
#define W_REPLACE ((uint8_t)3)
#define W_ROUTINE ((uint8_t)4) /* FUNCTION or PROCEDURE */
#define W_BEGIN ((uint8_t)5)
#define W_CASE ((uint8_t)6)
#define W_END ((uint8_t)7)

/* Whether the byte at `i` is the lowercase letter `want` in either case. */
/*@ requires valid_arena(a) && slice_storage(a, s) && 'a' <= want <= 'z';
    assigns \nothing;
*/
static bool letter_at(const sc_arena *a, sc_str s, size_t i, uint8_t want) {
    uint8_t b = 0;
    return sc_slice_get(a, s, i, &b) && (b == want || b == (uint8_t)(want - 0x20u));
}

/* The word s[off .. off + n), matched without case. */
/*@ requires valid_arena(a) && valid_slice(a, s) && off < s.len && n <= s.len - off;
    assigns \nothing;
*/
static uint8_t word_kind(const sc_arena *a, sc_str s, size_t off, size_t n) {
    size_t w = off;
    if (n == 2 && letter_at(a, s, w, 'o') && letter_at(a, s, w + 1, 'r')) {
        return W_OR;
    }
    if (n == 3 && letter_at(a, s, w, 'e') && letter_at(a, s, w + 1, 'n') &&
        letter_at(a, s, w + 2, 'd')) {
        return W_END;
    }
    if (n == 4 && letter_at(a, s, w, 'c') && letter_at(a, s, w + 1, 'a') &&
        letter_at(a, s, w + 2, 's') && letter_at(a, s, w + 3, 'e')) {
        return W_CASE;
    }
    if (n == 5 && letter_at(a, s, w, 'b') && letter_at(a, s, w + 1, 'e') &&
        letter_at(a, s, w + 2, 'g') && letter_at(a, s, w + 3, 'i') && letter_at(a, s, w + 4, 'n')) {
        return W_BEGIN;
    }
    if (n == 6 && letter_at(a, s, w, 'c') && letter_at(a, s, w + 1, 'r') &&
        letter_at(a, s, w + 2, 'e') && letter_at(a, s, w + 3, 'a') && letter_at(a, s, w + 4, 't') &&
        letter_at(a, s, w + 5, 'e')) {
        return W_CREATE;
    }
    if (n == 7 && letter_at(a, s, w, 'r') && letter_at(a, s, w + 1, 'e') &&
        letter_at(a, s, w + 2, 'p') && letter_at(a, s, w + 3, 'l') && letter_at(a, s, w + 4, 'a') &&
        letter_at(a, s, w + 5, 'c') && letter_at(a, s, w + 6, 'e')) {
        return W_REPLACE;
    }
    if (n == 8 && letter_at(a, s, w, 'f') && letter_at(a, s, w + 1, 'u') &&
        letter_at(a, s, w + 2, 'n') && letter_at(a, s, w + 3, 'c') && letter_at(a, s, w + 4, 't') &&
        letter_at(a, s, w + 5, 'i') && letter_at(a, s, w + 6, 'o') && letter_at(a, s, w + 7, 'n')) {
        return W_ROUTINE;
    }
    if (n == 9 && letter_at(a, s, w, 'p') && letter_at(a, s, w + 1, 'r') &&
        letter_at(a, s, w + 2, 'o') && letter_at(a, s, w + 3, 'c') && letter_at(a, s, w + 4, 'e') &&
        letter_at(a, s, w + 5, 'd') && letter_at(a, s, w + 6, 'u') && letter_at(a, s, w + 7, 'r') &&
        letter_at(a, s, w + 8, 'e')) {
        return W_ROUTINE;
    }
    return W_OTHER;
}

/* How far a statement's words outside parentheses have matched CREATE [OR
 * REPLACE] FUNCTION|PROCEDURE; like psql, other tokens do not count. */
#define P_START ((uint8_t)0)   /* nothing yet */
#define P_CREATE ((uint8_t)1)  /* CREATE */
#define P_OR ((uint8_t)2)      /* CREATE OR */
#define P_REPLACE ((uint8_t)3) /* CREATE OR REPLACE */
#define P_ROUTINE ((uint8_t)4) /* the whole prefix: BEGIN ... END nests */
#define P_NO ((uint8_t)5)      /* some other statement */

/* The prefix match after word `w`. */
/*@ assigns \nothing; */
static uint8_t prefix_next(uint8_t p, uint8_t w) {
    if (p == P_START && w == W_CREATE) {
        return P_CREATE;
    }
    if (p == P_CREATE && w == W_OR) {
        return P_OR;
    }
    if (p == P_OR && w == W_REPLACE) {
        return P_REPLACE;
    }
    if ((p == P_CREATE || p == P_REPLACE) && w == W_ROUTINE) {
        return P_ROUTINE;
    }
    return p == P_ROUTINE ? P_ROUTINE : P_NO;
}

/* BEGIN ... END depth after word `w` in a routine body, outside
 * parentheses. CASE also ends with END, so it nests once inside BEGIN. */
/*@ assigns \nothing; */
static size_t begin_next(size_t depth, uint8_t w) {
    if (w == W_BEGIN && depth < SIZE_MAX) {
        return depth + 1;
    }
    if (w == W_CASE && depth >= 1 && depth < SIZE_MAX) {
        return depth + 1;
    }
    if (w == W_END && depth > 0) {
        return depth - 1;
    }
    return depth;
}

size_t sqltok_stmt_end(const sc_arena *a, sc_str s, size_t from, bool std) {
    sqltok_state st;
    st.mode = SQLTOK_IN_NONE;
    st.std_strings = std;
    st = closed(a, s, st);
    size_t paren = 0;
    size_t begin = 0;
    uint8_t prefix = P_START;
    size_t off = from;
    /*@ loop invariant off == from || from < off <= s.len;
        loop invariant slice_storage(a, st.tag);
        loop assigns off, st, paren, begin, prefix;
        loop variant s.len - off;
    */
    while (off < s.len) {
        sqltok_token t = sqltok_step(a, s, off, st, true);
        if (t.kind == SQLTOK_NONE) {
            return s.len;
        }
        uint8_t b = 0;
        if (t.kind == SQLTOK_OP && sc_slice_get(a, s, off, &b)) {
            if (b == ';' && paren == 0 && begin == 0) {
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
            uint8_t w = word_kind(a, s, off, t.end - off);
            prefix = prefix_next(prefix, w);
            if (prefix == P_ROUTINE) {
                begin = begin_next(begin, w);
            }
        }
        off = t.end;
        st = t.next;
    }
    return s.len;
}
