#include "result.h"

size_t result_book_bytes(size_t cap) {
    /* Each allocation is rounded up to 8 bytes; one more word each for that. */
    return cap * sizeof(result_chunk) + cap * RESULT_MARKS * sizeof(size_t) +
           RESULT_MAX_COLUMNS * (sizeof(sc_str) + sizeof(uint32_t)) + 4u * 8u;
}

static void forget(result *r) {
    r->count = 0;
    r->has_columns = false;
    r->col_arena = NULL;
    r->col_index = 0;
    r->cols = 0;
    r->rows = 0;
}

bool result_init(result *r, sc_arena *book, size_t cap) {
    r->book = book;
    r->cap = cap;
    forget(r);
    r->chunks = SC_TSLICE_ALLOC(result_chunk, book, cap);
    r->marks = SC_TSLICE_ALLOC(size_t, book, cap * RESULT_MARKS);
    r->names = SC_TSLICE_ALLOC(sc_str, book, RESULT_MAX_COLUMNS);
    r->oids = SC_TSLICE_ALLOC(uint32_t, book, RESULT_MAX_COLUMNS);
    return cap > 0 && sc_tslice_len(r->chunks) == cap &&
           sc_tslice_len(r->marks) == cap * RESULT_MARKS &&
           sc_tslice_len(r->names) == RESULT_MAX_COLUMNS &&
           sc_tslice_len(r->oids) == RESULT_MAX_COLUMNS;
}

result_add result_set_columns(result *r, const sc_arena *a, size_t index) {
    proto_header h;
    proto_reader rd;
    uint64_t n = 0;
    if (r->has_columns || a == NULL || !proto_read(a, &h, &rd) || h.kind != PROTO_COLUMNS ||
        !proto_get_u64(&rd, &n) || n > RESULT_MAX_COLUMNS) {
        return RESULT_BAD;
    }
    for (size_t c = 0; c < (size_t)n; c++) {
        sc_str name;
        uint64_t oid = 0;
        if (!proto_get_str(&rd, &name) || !proto_get_u64(&rd, &oid) || oid > UINT32_MAX) {
            return RESULT_BAD;
        }
        uint32_t o = (uint32_t)oid;
        if (!SC_TSLICE_SET(sc_str, r->book, r->names, c, &name) ||
            !SC_TSLICE_SET(uint32_t, r->book, r->oids, c, &o)) {
            return RESULT_BAD;
        }
    }
    if (rd.pos != rd.body.len) {
        return RESULT_BAD;
    }
    r->has_columns = true;
    r->col_arena = a;
    r->col_index = index;
    r->cols = n;
    return RESULT_ADDED;
}

result_add result_add_chunk(result *r, const sc_arena *a, size_t index) {
    proto_header h;
    proto_reader rd;
    uint64_t first = r->rows;
    if (!r->has_columns || a == NULL || !proto_read(a, &h, &rd) ||
        h.kind != PROTO_CHUNK || h.arg == 0 || h.arg > UINT64_MAX - r->rows) {
        return RESULT_BAD;
    }
    if (r->count == r->cap) {
        return RESULT_FULL;
    }
    result_chunk k = {a, index, first, h.arg, h.arg / RESULT_MARKS + 1u};
    /* Walk every cell once: it checks the chunk and records the marks. */
    for (uint64_t row = 0; row < h.arg; row++) {
        if (row % k.stride == 0) {
            size_t at = r->count * RESULT_MARKS + (size_t)(row / k.stride);
            if (!SC_TSLICE_SET(size_t, r->book, r->marks, at, &rd.pos)) {
                return RESULT_BAD;
            }
        }
        for (uint64_t c = 0; c < r->cols; c++) {
            proto_cell cell;
            if (!proto_get_cell(&rd, &cell)) {
                return RESULT_BAD;
            }
        }
    }
    if (rd.pos != rd.body.len || !SC_TSLICE_SET(result_chunk, r->book, r->chunks, r->count, &k)) {
        return RESULT_BAD;
    }
    r->count++;
    r->rows += h.arg;
    return RESULT_ADDED;
}

size_t result_close(result *r, sc_arena *oa, sc_tslice out) {
    size_t n = 0;
    size_t room = sc_tslice_len(out);
    if (r->has_columns) {
        if (n < room) {
            (void)SC_TSLICE_SET(size_t, oa, out, n, &r->col_index);
        }
        n++;
    }
    for (size_t i = 0; i < r->count; i++) {
        result_chunk k;
        if (SC_TSLICE_GET(result_chunk, r->book, r->chunks, i, &k)) {
            if (n < room) {
                (void)SC_TSLICE_SET(size_t, oa, out, n, &k.index);
            }
            n++;
        }
    }
    forget(r);
    return n;
}

static bool chunk_at(const result *r, size_t i, result_chunk *k) {
    return SC_TSLICE_GET(result_chunk, r->book, r->chunks, i, k);
}

bool result_find(const result *r, uint64_t row, size_t *chunk, uint64_t *offset) {
    if (row >= r->rows || r->count == 0) {
        return false;
    }
    /* The last chunk whose first row is at most `row`. */
    size_t lo = 0, hi = r->count - 1u;
    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1u) / 2u;
        result_chunk k;
        if (!chunk_at(r, mid, &k)) {
            return false;
        }
        if (k.first <= row) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    result_chunk k;
    if (!chunk_at(r, lo, &k) || row - k.first >= k.rows) {
        return false;
    }
    *chunk = lo;
    *offset = row - k.first;
    return true;
}

bool result_column(const result *r, uint64_t c, sc_str *name, uint32_t *oid) {
    return r->has_columns && c < r->cols &&
           SC_TSLICE_GET(sc_str, r->book, r->names, (size_t)c, name) &&
           SC_TSLICE_GET(uint32_t, r->book, r->oids, (size_t)c, oid);
}

bool result_numeric(uint32_t oid) {
    /* int8, int2, int4, oid, float4, float8, numeric */
    return oid == 20u || oid == 21u || oid == 23u || oid == 26u || oid == 700u || oid == 701u ||
           oid == 1700u;
}

/* Opens chunk `i` at its mark at or before row `offset`; returns the rows to
 * skip from there. */
static bool open_chunk(result_cursor *c, size_t i, uint64_t offset, uint64_t *skip) {
    result_chunk k;
    proto_header h;
    size_t mark = 0;
    if (!chunk_at(c->r, i, &k) || offset >= k.rows) {
        return false;
    }
    uint64_t m = offset / k.stride;
    if (!proto_read(k.arena, &h, &c->rd) ||
        h.kind != PROTO_CHUNK ||
        !SC_TSLICE_GET(size_t, c->r->book, c->r->marks, i * RESULT_MARKS + (size_t)m, &mark)) {
        return false;
    }
    c->rd.pos = mark;
    c->chunk = i;
    c->left = k.rows - m * k.stride - 1u;
    c->cells = c->r->cols;
    *skip = offset - m * k.stride;
    return true;
}

bool result_next_cell(result_cursor *c, proto_cell *out) {
    if (c->cells == 0 || !proto_get_cell(&c->rd, out)) {
        return false;
    }
    c->cells--;
    return true;
}

bool result_next_row(result_cursor *c) {
    proto_cell cell;
    while (c->cells > 0) {
        if (!result_next_cell(c, &cell)) {
            return false;
        }
    }
    if (c->left > 0) {
        c->left--;
        c->cells = c->r->cols;
        return true;
    }
    uint64_t skip = 0;
    return c->chunk + 1u < c->r->count && open_chunk(c, c->chunk + 1u, 0, &skip);
}

bool result_seek(result_cursor *c, const result *r, uint64_t row) {
    size_t i = 0;
    uint64_t offset = 0, skip = 0;
    c->r = r;
    if (!result_find(r, row, &i, &offset) || !open_chunk(c, i, offset, &skip)) {
        return false;
    }
    for (uint64_t s = 0; s < skip; s++) {
        if (!result_next_row(c)) {
            return false;
        }
    }
    return true;
}

bool result_cell(const result *r, uint64_t row, uint64_t col, proto_cell *out) {
    result_cursor c;
    if (col >= r->cols || !result_seek(&c, r, row)) {
        return false;
    }
    for (uint64_t k = 0; k <= col; k++) {
        if (!result_next_cell(&c, out)) {
            return false;
        }
    }
    return true;
}

bool result_widths(const result *r, sc_arena *wa, sc_tslice widths, size_t n,
                   result_width_rule rule) {
    if (n > r->cols || rule.pad > rule.max_w) {
        return false;
    }
    for (size_t c = 0; c < n; c++) {
        sc_str name;
        uint32_t oid = 0;
        if (!result_column(r, c, &name, &oid)) {
            return false;
        }
        uint32_t w = rule.measure(rule.ctx, r->col_arena, name, RESULT_TEXT_HEADER);
        if (!SC_TSLICE_SET(uint32_t, wa, widths, c, &w)) {
            return false;
        }
    }
    result_chunk k;
    if (r->count > 0 && chunk_at(r, 0, &k)) {
        result_cursor cur;
        if (!result_seek(&cur, r, 0)) {
            return false;
        }
        for (uint64_t row = 0; row < k.rows && row < rule.rows; row++) {
            if (row > 0 && !result_next_row(&cur)) {
                return false;
            }
            for (size_t c = 0; c < n; c++) {
                proto_cell cell;
                uint32_t w = 0;
                if (!result_next_cell(&cur, &cell) ||
                    !SC_TSLICE_GET(uint32_t, wa, widths, c, &w)) {
                    return false;
                }
                uint32_t cw = rule.measure(rule.ctx, cur.rd.arena, cell.bytes,
                                           cell.null ? RESULT_TEXT_NULL : RESULT_TEXT_CELL);
                if (cw > w && !SC_TSLICE_SET(uint32_t, wa, widths, c, &cw)) {
                    return false;
                }
            }
        }
    }
    for (size_t c = 0; c < n; c++) {
        uint32_t w = 0;
        if (!SC_TSLICE_GET(uint32_t, wa, widths, c, &w)) {
            return false;
        }
        w = w > rule.max_w - rule.pad ? rule.max_w : w + rule.pad;
        if (!SC_TSLICE_SET(uint32_t, wa, widths, c, &w)) {
            return false;
        }
    }
    return true;
}
