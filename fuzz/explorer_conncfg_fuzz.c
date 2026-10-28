#include "../examples/explorer/conncfg.c"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

static alignas(8) uint8_t source[CONNCFG_BYTES + 1024], records[65536], output[2u * CONNCFG_BYTES + 1024];

int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t size) {
    if (size > CONNCFG_BYTES) return 0;
    sc_arena a, r, o;
    sc_arena_init(&a, source, sizeof source);
    sc_arena_init(&r, records, sizeof records);
    sc_arena_init(&o, output, sizeof output);
    sc_str input = sc_str_from_lit(&a, bytes, size);
    conncfg c;
    assert(conncfg_parse(&c, &a, input, &r) && c.count <= CONNCFG_MAX);
    for (size_t i = 0; i < c.count; i++) {
        conncfg_entry e;
        assert(conncfg_at(&c, i, &e));
        if (e.name.len) assert(e.name.ptr >= input.ptr && e.name.ptr + e.name.len <= input.ptr + input.len);
        if (e.error != CFG_OK) continue;
        for (size_t j = 0; j < e.count; j++) {
            conncfg_pair p;
            assert(SC_TSLICE_GET(conncfg_pair, &r, e.pairs, j, &p));
            assert(p.key.ptr >= input.ptr && p.key.ptr + p.key.len <= input.ptr + input.len);
            if (p.text.len) assert(p.text.ptr >= input.ptr && p.text.ptr + p.text.len <= input.ptr + input.len);
        }
        sc_str info;
        assert(sc_arena_reset(&o) && conncfg_info(&c, i, &o, &info));
    }
    assert(sc_arena_reset(&a));
    assert(sc_arena_reset(&r));
    assert(conncfg_parse(&c, &a, input, &r) && c.error == CFG_UTF8);
    return 0;
}
