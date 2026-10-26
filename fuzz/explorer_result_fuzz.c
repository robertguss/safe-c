/* Fuzzes the explorer's result model (examples/explorer/result.c).
 *
 * The input picks a column count, a quota and chunk sizes. Each row is its
 * row number (8 bytes) in column 0 and NULLs after it, so every read is
 * checked for the right row, not just for success. A chunk may have one byte
 * of its body flipped; result_add_chunk's verdict must then match a reference
 * walk of the same bytes written from proto.h with plain loads, and a flip
 * that leaves it well formed only exempts its rows from the row-number check. Lookups and
 * cursor walks are checked against a linear scan of the chunks accepted.
 * Ends by closing (every index once, COLUMNS first) and the stale path. */
#include "../examples/explorer/proto.c"
#include "../examples/explorer/result.c"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

#define CHUNKS 6u
#define CHUNK_BYTES (16u * 1024u)

static alignas(8) uint8_t pool_mem[CHUNKS + 1u][CHUNK_BYTES];
static alignas(8) uint8_t book_mem[1u << 17];
static alignas(8) uint8_t out_mem[256];

static uint64_t word(const uint8_t *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++) {
        v |= (uint64_t)p[i] << (8 * i);
    }
    return v;
}

/* The reference: whether the chunk body at p (n bytes) is exactly `rows`
 * rows of `cols` well-formed cells. */
static bool ref_chunk(const uint8_t *p, size_t n, uint64_t rows, uint64_t cols) {
    size_t at = 0;
    for (uint64_t k = 0; k < rows * cols; k++) {
        if (n - at < 16) {
            return false;
        }
        uint64_t meta = word(p + at), len = word(p + at + 8);
        at += 16;
        if (len > n - at) {
            return false;
        }
        bool null = (meta & PROTO_CELL_NULL) != 0, trunc = (meta & PROTO_CELL_TRUNCATED) != 0;
        uint64_t truelen = meta & ~(PROTO_CELL_NULL | PROTO_CELL_TRUNCATED);
        bool short_ok = trunc ? len < truelen : len == truelen;
        if (len > PROTO_CELL_CAP || !short_ok || (null && (truelen != 0 || trunc))) {
            return false;
        }
        at += (size_t)len;
    }
    return at == n;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) {
        return 0;
    }
    static sc_arena pool[CHUNKS + 1u];
    sc_arena book, oa;
    sc_arena_init(&book, book_mem, sizeof book_mem);
    sc_arena_init(&oa, out_mem, sizeof out_mem);
    size_t n = data[0] % (CHUNKS + 1u);
    size_t cap = (size_t)(data[0] / 7u) % (CHUNKS + 1u) + 1u;
    uint64_t cols = 1u + data[1] % 3u;
    result r;
    assert(result_init(&r, &book, cap));
    sc_arena *ca = &pool[CHUNKS];
    sc_arena_init(ca, pool_mem[CHUNKS], CHUNK_BYTES);
    proto_writer w;
    proto_header h = {PROTO_COLUMNS, 1, 1, 0, 0, 0};
    sc_str none = {NULL, 0, ca->epoch};
    assert(proto_begin(&w, ca, h) && proto_put_u64(&w, cols));
    for (uint64_t c = 0; c < cols; c++) {
        assert(proto_put_str(&w, ca, none) && proto_put_u64(&w, c == 0 ? 20u : 25u));
    }
    assert(proto_end(&w) && result_set_columns(&r, ca, CHUNKS) == RESULT_ADDED);
    uint64_t firsts[CHUNKS] = {0}, total = 0;
    size_t idx[CHUNKS] = {0}, added = 0, used = 2;
    bool bent[CHUNKS] = {false}; /* a flip that left the chunk well formed */
    for (size_t k = 0; k < n; k++) {
        uint8_t b0 = used < size ? data[used] : 0, b1 = used + 1 < size ? data[used + 1] : 0;
        used += 2;
        uint64_t rows = b0 % 150u; /* 0 is a malformed chunk */
        sc_arena *a = &pool[k];
        sc_arena_init(a, pool_mem[k], CHUNK_BYTES);
        h.kind = PROTO_CHUNK;
        h.arg = rows;
        assert(proto_begin(&w, a, h));
        for (uint64_t row = 0; row < rows; row++) {
            uint64_t v = total + row;
            uint8_t enc[8];
            for (unsigned i = 0; i < 8; i++) {
                enc[i] = (uint8_t)(v >> (8 * i));
            }
            alignas(8) uint8_t tmp_mem[16];
            sc_arena tmp;
            sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
            assert(proto_put_u64(&w, 8) && proto_put_str(&w, &tmp, sc_str_from_lit(&tmp, enc, 8)));
            for (uint64_t c = 1; c < cols; c++) {
                assert(proto_put_u64(&w, PROTO_CELL_NULL) && proto_put_str(&w, a, none));
            }
        }
        assert(proto_end(&w));
        size_t body = (size_t)word(pool_mem[k] + 48); /* the header's body length */
        bool flipped = b1 >= 200u && body > 0;
        if (flipped) { /* flip one body byte */
            uint8_t *p = pool_mem[k] + PROTO_HEADER_BYTES + (size_t)b1 * 131u % body;
            *p = (uint8_t)(*p ^ (1u + b0));
        }
        bool want_ok = rows > 0 && ref_chunk(pool_mem[k] + PROTO_HEADER_BYTES, body, rows, cols);
        result_add got = result_add_chunk(&r, a, k);
        /* A full index refuses before it reads any cell. */
        if (added == cap && rows > 0) {
            assert(got == RESULT_FULL);
            break;
        }
        if (!want_ok) {
            assert(got == RESULT_BAD);
            break; /* a flipped row number would break the scan below */
        }
        assert(got == RESULT_ADDED);
        bent[added] = flipped;
        idx[added] = k;
        firsts[added++] = total;
        total += rows;
    }
    assert(r.rows == total && r.count == added);
    for (size_t i = used; i + 8u <= size; i += 8u) {
        uint64_t row = word(data + i) % (total + 2u);
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
            proto_cell cell;
            size_t in = 0;
            while (in + 1u < added && firsts[in + 1u] <= k) {
                in++;
            }
            assert(result_next_cell(&c, &cell));
            assert(bent[in] || (cell.bytes.len == 8 && word(cell.bytes.ptr) == k));
            for (uint64_t col = 1; col < cols; col++) {
                assert(result_next_cell(&c, &cell) && (bent[in] || cell.null));
            }
            assert(!result_next_cell(&c, &cell));
        }
        if (row + 40u >= total) {
            result_cursor end;
            assert(result_seek(&end, &r, total - 1u) && !result_next_row(&end));
        }
    }
    /* The stale path: a chunk arena reset under the result fails its reads. */
    if (added > 0) {
        assert(sc_arena_reset(&pool[idx[0]]));
        proto_cell cell;
        assert(!result_cell(&r, 0, 0, &cell));
    }
    sc_tslice out = SC_TSLICE_ALLOC(size_t, &oa, CHUNKS + 1u);
    assert(result_close(&r, &oa, out) == added + 1u);
    size_t v = 0;
    assert(SC_TSLICE_GET(size_t, &oa, out, 0, &v) && v == CHUNKS);
    for (size_t i = 0; i < added; i++) {
        assert(SC_TSLICE_GET(size_t, &oa, out, i + 1u, &v) && v == idx[i]);
    }
    assert(r.rows == 0 && r.count == 0 && !r.has_columns);
    return 0;
}
