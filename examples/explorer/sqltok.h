#ifndef EXPLORER_SQLTOK_H
#define EXPLORER_SQLTOK_H

/* The editor's SQL tokenizer and statement splitter (plan U14). Core code: it
 * includes only the stdlib, so tests/explorer_sqltok_test.c links it alone.
 *
 * The lexer is resumable: one step lexes one token from `off` in a state and
 * returns its kind, its end and the state after it, so the editor can re-lex
 * from a saved checkpoint (plan KTD25). The rules follow psql's lexer
 * (src/fe_utils/psqlscan.l). */

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

/* `depth` is the block-comment nesting; `tag` the open dollar tag, a slice
 * of the input (the caller may replace it with a copy elsewhere in the arena
 * `a` that the step reads both through);
 * `std_strings` is standard_conforming_strings. A zeroed state with
 * std_strings set is the start of input. */
typedef struct {
    uint8_t mode;
    size_t depth;
    uint8_t quote;
    sc_str tag;
    bool std_strings;
} sqltok_state;

typedef struct {
    uint8_t kind;
    size_t end;
    sqltok_state next;
} sqltok_token;

/* Lexes one token of `s` from `off` in state `st`. `at_end` says that `s`
 * ends the input. Without it, a string, identifier, dollar string or block
 * comment still open at the end of `s` comes back as its kind up to where
 * more input could change it, with the open state in `next`; any other token
 * that could still grow comes back as SQLTOK_NONE. With it, an open construct
 * is SQLTOK_UNTERMINATED up to the end, with the open state in `next`.
 * SQLTOK_NONE has end == off and next == st; it is also the result at or past
 * the end, for a stale `s`, and in a dollar string whose tag is stale.
 * Any other kind is a non-empty token inside `s` (off < end <= s.len), so
 * stepping from each token's end tiles `s` with no gap or overlap. The caller
 * guarantees `s` and st.tag are storage of `a`. */
/*@ predicate sqltok_result(sc_arena *a, sc_str s, size_t off, sqltok_state st,
                            sqltok_token t) =
      slice_storage(a, t.next.tag) &&
      (t.kind == SQLTOK_NONE ==> t.end == off && t.next == st) &&
      (t.kind != SQLTOK_NONE ==> valid_slice(a, s) && off < t.end <= s.len);
*/
/*@ requires valid_arena(a) && slice_storage(a, s) && slice_storage(a, st.tag);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures sqltok_result(a, s, off, st, \result);
*/
sqltok_token sqltok_step(const sc_arena *a, sc_str s, size_t off, sqltok_state st, bool at_end);

/* The offset of the `;` that ends the statement starting at `from`, or s.len
 * when none does. A `;` ends a statement only outside parentheses and outside
 * BEGIN ... END in CREATE [OR REPLACE] FUNCTION or PROCEDURE. `std` is
 * standard_conforming_strings. A `from` past s.len gives s.len. The caller
 * guarantees `s` is storage of `a`. */
/*@ requires valid_arena(a) && slice_storage(a, s);
    terminates \true;
    exits \false;
    assigns \nothing;
    ensures from <= s.len ==> from <= \result <= s.len;
    ensures from > s.len ==> \result == s.len;
    ensures \result != s.len ==> valid_slice(a, s) && s.ptr[\result] == ';';
*/
size_t sqltok_stmt_end(const sc_arena *a, sc_str s, size_t from, bool std);

#endif
