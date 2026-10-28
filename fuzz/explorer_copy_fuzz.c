#include "../examples/explorer/proto.c"
#include "../examples/explorer/result.c"
#include "../examples/explorer/copy.c"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 4096) {
        return 0;
    }
    static alignas(8) uint8_t input[8192], output[16384];
    sc_arena a, out;
    sc_arena_init(&a, input, sizeof input);
    sc_arena_init(&out, output, sizeof output);
    bool null = (data[0] & 1u) != 0, csv = (data[0] & 2u) != 0;
    sc_str s = sc_str_from_lit(&a, data + 1, size - 1);
    size_t cap = (data[0] & 4u) != 0 ? size / 2 : sizeof output;
    sc_buf b = sc_buf_new(sc_arena_alloc(&out, cap));
    bool ok = copy_field(&out, &b, &a, s, null, false, csv);
    uint8_t expected[8194];
    size_t n = 0;
    if (!null) {
        if (csv) expected[n++] = '"';
        for (size_t i = 1; i < size; i++) {
            uint8_t c = data[i];
            if (csv && c == '"') expected[n++] = '"';
            if (!csv && (c == '\t' || c == '\n' || c == '\r')) c = ' ';
            expected[n++] = c;
        }
        if (csv) expected[n++] = '"';
    }
    assert(ok == (n <= cap));
    if (ok) {
        assert(b.len == n && (n == 0 || memcmp(b.dst.ptr, expected, n) == 0));
        if (csv && !null) {
            uint8_t decoded[4096];
            size_t used = 0, at = 1;
            assert(n >= 2 && expected[0] == '"' && expected[n - 1] == '"');
            while (at < n - 1) {
                uint8_t c = b.dst.ptr[at++];
                if (c == '"') assert(at < n - 1 && b.dst.ptr[at++] == '"');
                decoded[used++] = c;
            }
            assert(used == size - 1 && memcmp(decoded, data + 1, used) == 0);
        }
    }
    assert(sc_arena_reset(&a));
    assert(!copy_field(&out, &b, &a, s, null, false, csv));
    return 0;
}
