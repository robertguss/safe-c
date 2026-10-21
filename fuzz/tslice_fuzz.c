#include "safec/tslice.h"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

struct rec {
    uint64_t a;
    uint8_t b;
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4)
        return 0;
    alignas(8) uint8_t storage[128];
    uint8_t expected[128];
    memset(storage, 0, sizeof storage);
    memset(expected, 0, sizeof expected);
    sc_arena a;
    sc_arena_init(&a, storage, sizeof storage);
    size_t count = data[0] % 9;
    sc_tslice t = SC_TSLICE_ALLOC(struct rec, &a, count);
    assert(sc_tslice_len(t) == count);
    for (size_t pos = 1; pos + 3 <= size; pos += 3) {
        size_t i = data[pos] % (count + 2);
        struct rec in;
        memset(&in, 0, sizeof in);
        in.a = (uint64_t)data[pos + 1] * UINT64_C(0x0101010101010101);
        in.b = data[pos + 2];
        struct rec out;
        memset(&out, 0xaa, sizeof out);
        if (data[pos + 2] % 2 == 0) {
            bool ok = SC_TSLICE_SET(struct rec, &a, t, i, &in);
            assert(ok == (i < count));
            if (ok)
                memcpy(expected + i * sizeof in, &in, sizeof in);
        } else {
            bool ok = SC_TSLICE_GET(struct rec, &a, t, i, &out);
            assert(ok == (i < count));
            if (ok)
                assert(memcmp(&out, expected + i * sizeof out, sizeof out) == 0);
            else
                assert(out.b == 0xaa);
        }
        assert(memcmp(storage, expected, count * sizeof(struct rec)) == 0);
    }
    assert(sc_arena_reset(&a));
    struct rec out;
    if (count > 0)
        assert(!SC_TSLICE_GET(struct rec, &a, t, 0, &out));
    return 0;
}
