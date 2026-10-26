/* Fuzzes the explorer's result model (examples/explorer/result.c): random
 * chunk sizes, then lookups and cursor walks checked against a linear scan of
 * the same chunk list. Each row is a NULL then its row number as 8 bytes, so every read
 * is checked for the right row, not just for success. */
#include "../examples/explorer/proto.c"
#include "../examples/explorer/result.c"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

#define CHUNKS 6u
#define CHUNK_BYTES (64u * 1024u)

static alignas(8) uint8_t pool_mem[CHUNKS + 1u][CHUNK_BYTES];
static alignas(8) uint8_t book_mem[1u << 17];

static uint64_t word(const uint8_t *p) {
    uint64_t v = 0;
    memcpy(&v, p, 8);
    return v;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) {
        return 0;
    }
    static sc_arena pool[CHUNKS + 1u];
    sc_arena book;
    sc_arena_init(&book, book_mem, sizeof book_mem);
    size_t n = data[0] % (CHUNKS + 1u);
    size_t cap = (size_t)(data[0] / 7u) % (CHUNKS + 1u) + 1u;
    result r;
    assert(result_init(&r, &book, cap));
    /* COLUMNS: two columns. */
    sc_arena_init(&pool[CHUNKS], pool_mem[CHUNKS], CHUNK_BYTES);
    proto_writer w;
    proto_header h = {PROTO_COLUMNS, 1, 1, 0, 0, 0};
    sc_str none = {NULL, 0, pool[CHUNKS].epoch};
    assert(proto_begin(&w, &pool[CHUNKS], h) && proto_put_u64(&w, 2) &&
           proto_put_str(&w, &pool[CHUNKS], none) && proto_put_u64(&w, 23) &&
           proto_put_str(&w, &pool[CHUNKS], none) && proto_put_u64(&w, 25) && proto_end(&w));
    assert(result_set_columns(&r, &pool[CHUNKS], CHUNKS) == RESULT_ADDED);
    uint64_t firsts[CHUNKS + 1u] = {0}, total = 0;
    size_t added = 0;
    for (size_t k = 0; k < n; k++) {
        uint64_t rows = 1u + (k + 1u < size ? data[k + 1u] : 0u) * 7u % 600u;
        sc_arena *a = &pool[k];
        sc_arena_init(a, pool_mem[k], CHUNK_BYTES);
        h.kind = PROTO_CHUNK;
        h.arg = rows;
        assert(proto_begin(&w, a, h));
        for (uint64_t row = 0; row < rows; row++) {
            uint64_t v = total + row;
            assert(proto_put_u64(&w, PROTO_CELL_NULL) && proto_put_str(&w, a, none));
            uint8_t enc[8];
            memcpy(enc, &v, 8);
            sc_arena tmp;
            alignas(8) uint8_t tmp_mem[16];
            sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
            assert(proto_put_u64(&w, 8) && proto_put_str(&w, &tmp, sc_str_from_lit(&tmp, enc, 8)));
        }
        assert(proto_end(&w));
        result_add got = result_add_chunk(&r, a, k, total);
        if (added == cap) {
            assert(got == RESULT_FULL);
            break;
        }
        assert(got == RESULT_ADDED);
        firsts[added++] = total;
        total += rows;
    }
    assert(r.rows == total && r.count == added);
    for (size_t i = 1u + n; i + 8u <= size; i += 8u) {
        uint64_t row = total > 0 ? word(data + i) % (total + 2u) : word(data + i) % 3u;
        size_t ch = 0;
        uint64_t off = 0;
        bool found = result_find(&r, row, &ch, &off);
        assert(found == (row < total));
        if (!found) {
            continue;
        }
        size_t want = 0;
        while (want + 1u < added && firsts[want + 1u] <= row) {
            want++;
        }
        assert(ch == want && off == row - firsts[want]);
        result_cursor c;
        assert(result_seek(&c, &r, row));
        for (uint64_t k = row; k < row + 40u && k < total; k++) {
            if (k > row) {
                assert(result_next_row(&c));
            }
            proto_cell nul, val;
            assert(result_next_cell(&c, &nul) && nul.null);
            assert(result_next_cell(&c, &val) && val.bytes.len == 8);
            uint64_t v = 0;
            memcpy(&v, val.bytes.ptr, 8);
            assert(v == k);
            assert(!result_next_cell(&c, &val));
        }
    }
    return 0;
}
