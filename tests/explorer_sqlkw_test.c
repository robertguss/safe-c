/* Tests the explorer's SQL keyword set (examples/explorer/sqlkw.c, plan U14):
 * case-insensitive lookup, the binary search's ends and a word between two
 * keywords, a list that is unsorted or repeats a word, and an arena too small.
 * Core code, built in. */
#include "../examples/explorer/sqlkw.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>

alignas(8) static uint8_t mem[16384];
alignas(8) static uint8_t words_mem[256];
alignas(8) static uint8_t small_mem[2048];
static sc_arena A, W, S;

static bool is(const sqlkw *k, sc_str w) {
    return sqlkw_is(k, &W, w);
}

static void lookups(void) {
    sqlkw k;
    assert(sqlkw_init(&k, &A, SQLTOK_POSTGRESQL));
    assert(is(&k, SC_STR_LIT(&W, "SELECT")));
    assert(is(&k, SC_STR_LIT(&W, "select")));
    assert(is(&k, SC_STR_LIT(&W, "Select")));
    assert(!is(&k, SC_STR_LIT(&W, "selects")));
    sc_str empty = {NULL, 0, W.epoch};
    assert(!is(&k, empty));
    /* The list's first and last words, and past each end. */
    assert(is(&k, SC_STR_LIT(&W, "abort")));
    assert(is(&k, SC_STR_LIT(&W, "WITH")));
    assert(!is(&k, SC_STR_LIT(&W, "AB")));
    assert(!is(&k, SC_STR_LIT(&W, "WITHOUT")));
    assert(!is(&k, SC_STR_LIT(&W, "zzz")));
    /* Between CURRENT_DATE and CURRENT_ROLE, and a prefix of CURRENT_USER. */
    assert(is(&k, SC_STR_LIT(&W, "current_date")));
    assert(!is(&k, SC_STR_LIT(&W, "current_name")));
    assert(!is(&k, SC_STR_LIT(&W, "current_use")));
    assert(!is(&k, SC_STR_LIT(&W, "col umn")));
    /* A stale word matches nothing. */
    sc_str stale = SC_STR_LIT(&W, "FROM");
    assert(is(&k, stale));
    assert(sc_arena_reset(&W));
    assert(!is(&k, stale));
}

static void every_word(void) {
    sqlkw k;
    assert(sqlkw_init(&k, &A, SQLTOK_POSTGRESQL));
    size_t n = sc_tslice_len(k.words);
    assert(n > 100u);
    for (size_t i = 0; i < n; i++) {
        sc_str kw = {NULL, 0, 0};
        assert(SC_TSLICE_GET(sc_str, &A, k.words, i, &kw) && kw.len < 32u);
        uint8_t lower[32], mixed[32];
        for (size_t j = 0; j < kw.len; j++) {
            uint8_t c = kw.ptr[j];
            uint8_t l = (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 32) : c;
            lower[j] = l;
            mixed[j] = (j % 2u) ? c : l;
        }
        assert(sc_arena_reset(&W));
        assert(is(&k, sc_str_from_lit(&W, kw.ptr, kw.len)));
        assert(is(&k, sc_str_from_lit(&W, lower, kw.len)));
        assert(is(&k, sc_str_from_lit(&W, mixed, kw.len)));
    }
}

static void rejects_bad_lists(void) {
    sqlkw k;
    assert(sqlkw_init_list(&k, &A, SC_STR_LIT(&A, "AND AS FROM")));
    assert(is(&k, SC_STR_LIT(&W, "as")));
    assert(!is(&k, SC_STR_LIT(&W, "or")));
    assert(!sqlkw_init_list(&k, &A, SC_STR_LIT(&A, "AS AND")));
    assert(!sqlkw_init_list(&k, &A, SC_STR_LIT(&A, "AND AND")));
    assert(!sqlkw_init_list(&k, &A, SC_STR_LIT(&A, "and AND")));
    assert(!sqlkw_init_list(&k, &A, SC_STR_LIT(&A, "AND  AS")));
    assert(!sqlkw_init_list(&k, &A, SC_STR_LIT(&A, "AND AS ")));
    sc_str empty = {NULL, 0, A.epoch};
    assert(!sqlkw_init_list(&k, &A, empty));
}

/* Room for the literal but not the word table. */
static void too_small(void) {
    sqlkw k;
    assert(!sqlkw_init(&k, &S, SQLTOK_POSTGRESQL));
}

static void dialect_words(void) {
    sqlkw k;
    assert(sc_arena_reset(&A) && sc_arena_reset(&W));
    assert(sqlkw_init(&k, &A, SQLTOK_SQLITE));
    assert(is(&k, SC_STR_LIT(&W, "pragma")) && is(&k, SC_STR_LIT(&W, "without")));
    assert(!is(&k, SC_STR_LIT(&W, "ilike")) && !is(&k, SC_STR_LIT(&W, "qualify")));
    assert(sc_arena_reset(&A));
    assert(sqlkw_init(&k, &A, SQLTOK_DUCKDB));
    assert(is(&k, SC_STR_LIT(&W, "qualify")) && is(&k, SC_STR_LIT(&W, "pivot")));
    assert(!is(&k, SC_STR_LIT(&W, "isqlite")));
}

int main(void) {
    sc_arena_init(&A, mem, sizeof mem);
    sc_arena_init(&W, words_mem, sizeof words_mem);
    sc_arena_init(&S, small_mem, sizeof small_mem);
    lookups();
    every_word();
    rejects_bad_lists();
    too_small();
    dialect_words();
    puts("explorer_sqlkw_test: ok");
    return 0;
}
