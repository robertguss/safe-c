#include "safec/tslice.h"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

struct fix {
    const char *title;
    uint32_t first;
    uint16_t count;
};

int main(void) {
    alignas(8) uint8_t storage[256] = {0};
    alignas(8) uint8_t other_storage[8] = {0};
    sc_arena a, other;
    sc_arena_init(&a, storage, sizeof storage);
    sc_arena_init(&other, other_storage, sizeof other_storage);

    sc_tslice none = SC_TSLICE_ALLOC(uint64_t, &a, 0);
    assert(none.bytes.ptr == NULL && none.elem == 0 && sc_tslice_len(none) == 0);
    uint64_t w = 7;
    assert(!SC_TSLICE_GET(uint64_t, &a, none, 0, &w) && w == 7);
    assert(!SC_TSLICE_SET(uint64_t, &a, none, 0, &w));
    assert(sc_tslice_alloc(&a, SIZE_MAX, 8).bytes.ptr == NULL);
    assert(sc_tslice_alloc(&a, 1, 0).bytes.ptr == NULL);
    assert(sc_tslice_alloc(&a, 1000, 8).bytes.ptr == NULL);

    sc_tslice words = SC_TSLICE_ALLOC(uint64_t, &a, 4);
    assert(sc_tslice_len(words) == 4 && words.elem == 8);
    for (uint64_t i = 0; i < 4; i++) {
        uint64_t v = (i + 1) * UINT64_C(0x0101010101010101);
        assert(SC_TSLICE_SET(uint64_t, &a, words, i, &v));
    }
    assert(SC_TSLICE_GET(uint64_t, &a, words, 3, &w) && w == UINT64_C(0x0404040404040404));
    w = 7;
    assert(!SC_TSLICE_GET(uint64_t, &a, words, 4, &w) && w == 7);
    assert(!SC_TSLICE_GET(uint64_t, &a, words, SIZE_MAX, &w) && w == 7);
    assert(!SC_TSLICE_SET(uint64_t, &a, words, 4, &w));
    uint32_t narrow = 9;
    assert(!sc_tslice_get(&a, words, 0, sizeof narrow, &narrow) && narrow == 9);
    assert(!sc_tslice_get(&a, words, 0, 0, &narrow));

    sc_tslice fixes = SC_TSLICE_ALLOC(struct fix, &a, 3);
    assert(sc_tslice_len(fixes) == 3 && fixes.elem == sizeof(struct fix));
    struct fix f = {"title", 40, 2};
    assert(SC_TSLICE_SET(struct fix, &a, fixes, 1, &f));
    struct fix zero;
    memset(&zero, 0, sizeof zero);
    struct fix g = zero;
    assert(SC_TSLICE_GET(struct fix, &a, fixes, 0, &g) && g.title == NULL && g.first == 0);
    assert(SC_TSLICE_GET(struct fix, &a, fixes, 1, &g));
    assert(g.title == f.title && g.first == 40 && g.count == 2);
    assert(SC_TSLICE_GET(struct fix, &a, fixes, 2, &g) && g.title == NULL);
    /* Setting one element leaves its neighbours alone, and the words intact. */
    assert(SC_TSLICE_GET(uint64_t, &a, words, 0, &w) && w == UINT64_C(0x0101010101010101));

    assert(sc_arena_reset(&other));
    assert(SC_TSLICE_GET(struct fix, &a, fixes, 1, &g));
    assert(sc_arena_reset(&a));
    g = f;
    assert(!SC_TSLICE_GET(struct fix, &a, fixes, 1, &g) && g.first == 40);
    assert(!SC_TSLICE_SET(struct fix, &a, fixes, 1, &zero));
    sc_tslice fresh = SC_TSLICE_ALLOC(uint64_t, &a, 4);
    assert(fresh.bytes.ptr == words.bytes.ptr);
    assert(!SC_TSLICE_GET(uint64_t, &a, words, 0, &w));
    assert(SC_TSLICE_GET(uint64_t, &a, fresh, 0, &w));
    return 0;
}
