#include "../examples/explorer/history.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static alignas(8) uint8_t data[HISTORY_BYTES], records[30000], source[HISTORY_FILE_BYTES + 1024], disk[HISTORY_FILE_BYTES];

int main(void) {
    sc_arena a, r, s, d;
    sc_arena_init(&a, data, sizeof data);
    sc_arena_init(&r, records, sizeof records);
    sc_arena_init(&s, source, sizeof source);
    sc_arena_init(&d, disk, sizeof disk);
    history h;
    assert(history_init(&h, &a, &r));
    sc_str sql = SC_STR_LIT(&s, "select 1;\n@12 1700000000000 prod\n'日本語'"), name = SC_STR_LIT(&s, "prod\nblue");
    assert(history_add(&h, &s, sql, &s, name, 1700000000000u));
    assert(history_add(&h, &s, sql, &s, name, 2) && h.count == 1);
    assert(history_add(&h, &s, SC_STR_LIT(&s, "select 2"), &s, name, 3));
    assert(history_add(&h, &s, SC_STR_LIT(&s, "select 3"), &s, name, 4));
    sc_str file;
    assert(history_write(&h, &d, &file));
    assert(history_load(&h, &d, file) && h.count == 3 && !h.damaged && !h.dirty);
    history_entry e;
    assert(history_get(&h, 0, &e) && e.time == 1700000000000u && e.sql.len == sql.len &&
           memcmp(e.sql.ptr, sql.ptr, sql.len) == 0 && e.name.len == name.len);
    assert(history_load(&h, &d, sc_slice_sub(&d, file, 0, file.len - 4u)) && h.count == 2 && h.damaged);
    for (size_t i = 0; i < 501; i++) {
        assert(sc_arena_reset(&s));
        char text[32];
        int n = snprintf(text, sizeof text, "select %zu", i);
        assert(history_add(&h, &s, sc_str_from_lit(&s, (uint8_t *)text, (size_t)n), &s,
                           SC_STR_LIT(&s, "dev"), i));
    }
    assert(h.count == 500 && history_get(&h, 0, &e) && e.time == 1);
    assert(sc_arena_reset(&s));
    sc_slice big = sc_arena_alloc(&s, HISTORY_BYTES - 3u);
    memset(big.ptr, 'x', big.len);
    assert(history_add(&h, &s, big, &s, SC_STR_LIT(&s, "dev"), 99) && h.count == 1 && h.used == HISTORY_BYTES);
    assert(sc_arena_reset(&d) && history_write(&h, &d, &file));
    assert(history_load(&h, &d, file) && h.count == 1 && !h.damaged);
    assert(sc_arena_reset(&s) && !history_add(&h, &s, big, &s, SC_STR_LIT(&s, "dev"), 0));
    puts("explorer_history_test: ok");
}
