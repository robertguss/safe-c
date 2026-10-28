#include "../examples/explorer/history.c"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

static alignas(8) uint8_t data[HISTORY_BYTES], records[30000], source[HISTORY_FILE_BYTES], disk[HISTORY_FILE_BYTES];

int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t size) {
    sc_arena a, r, s, d;
    sc_arena_init(&a, data, sizeof data);
    sc_arena_init(&r, records, sizeof records);
    sc_arena_init(&s, source, sizeof source);
    sc_arena_init(&d, disk, sizeof disk);
    history h;
    assert(history_init(&h, &a, &r));
    if (size > sizeof source) return 0;
    sc_str file = sc_str_from_lit(&s, bytes, size);
    assert(history_load(&h, &s, file));
    assert(h.count <= HISTORY_COUNT && h.used <= HISTORY_BYTES);
    sc_str written;
    assert(history_write(&h, &d, &written));
    size_t count = h.count, used = h.used;
    assert(history_load(&h, &d, written) && !h.damaged && h.count == count && h.used == used);
    h.count = h.used = 0;
    if (size > 0 && size < HISTORY_BYTES - 4u) {
        sc_str name = SC_STR_LIT(&s, "dev");
        assert(history_add(&h, &s, file, &s, name, 17));
        assert(sc_arena_reset(&d) && history_write(&h, &d, &written));
        assert(history_load(&h, &d, written) && h.count == 1 && !h.damaged);
        history_entry e;
        assert(history_get(&h, 0, &e) && e.time == 17 && e.sql.len == size &&
               memcmp(e.sql.ptr, bytes, size) == 0 && e.name.len == 3 && memcmp(e.name.ptr, "dev", 3) == 0);
        assert(sc_arena_reset(&s) && !history_add(&h, &s, file, &s, name, 0));
    }
    return 0;
}
