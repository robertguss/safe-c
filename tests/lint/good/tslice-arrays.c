#include <safec/tslice.h>

/* Mo-shaped tables with no subscript and no raw array: a struct table
 * (diag.c's fixes) and a table of 64-bit words (decimal.c's multiply). */
struct fix {
    uint32_t first;
    uint32_t count;
};

static bool record_fix(sc_arena *a, sc_tslice fixes, size_t i, uint32_t first) {
    struct fix f = {first, 1};
    if (!SC_TSLICE_SET(struct fix, a, fixes, i, &f)) {
        return false;
    }
    struct fix back = {0, 0};
    return SC_TSLICE_GET(struct fix, a, fixes, i, &back) && back.first == first;
}

static bool add_words(sc_arena *a, sc_tslice words, size_t i, size_t j) {
    uint64_t x = 0;
    uint64_t y = 0;
    if (!SC_TSLICE_GET(uint64_t, a, words, i, &x) ||
        !SC_TSLICE_GET(uint64_t, a, words, j, &y)) {
        return false;
    }
    uint64_t sum = x + y;
    return SC_TSLICE_SET(uint64_t, a, words, i, &sum);
}

int tslice_user(sc_arena *a) {
    sc_tslice fixes = SC_TSLICE_ALLOC(struct fix, a, 4);
    sc_tslice words = SC_TSLICE_ALLOC(uint64_t, a, 4);
    for (size_t i = 0; i < sc_tslice_len(fixes); i++) {
        if (!record_fix(a, fixes, i, 7)) {
            return 1;
        }
    }
    return add_words(a, words, 0, 1) ? 0 : 1;
}
