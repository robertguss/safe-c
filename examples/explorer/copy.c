#include "copy.h"

bool copy_field(sc_arena *a, sc_buf *b, const sc_arena *sa, sc_str s, bool null,
                bool numeric, bool csv) {
    if (s.epoch != sa->epoch || sc_slice_sub(sa, s, 0, s.len).len != s.len) {
        return false;
    }
    if (null) {
        return true;
    }
    bool quoted = csv && !numeric;
    if (quoted && !SC_BUF_LIT(a, b, "\"")) {
        return false;
    }
    for (size_t i = 0; i < s.len; i++) {
        uint8_t ch = 0;
        if (!sc_slice_get(sa, s, i, &ch)) {
            return false;
        }
        if (!csv && (ch == '\t' || ch == '\n' || ch == '\r')) {
            ch = ' ';
        }
        if ((quoted && ch == '"' && !SC_BUF_LIT(a, b, "\"")) ||
            !sc_buf_lit(a, b, &ch, 1)) {
            return false;
        }
    }
    return !quoted || SC_BUF_LIT(a, b, "\"");
}

bool copy_rows(sc_arena *a, size_t cap, const result *r, uint64_t first, uint64_t last,
               bool csv, sc_str *out, uint64_t *truncated) {
    if (first > last || last >= r->rows || r->cols == 0) {
        return false;
    }
    sc_buf b = sc_buf_new(sc_arena_alloc(a, cap));
    if (b.dst.len != cap) {
        return false;
    }
    if (csv) {
        for (uint64_t c = 0; c < r->cols; c++) {
            sc_str name;
            result_type type;
            if ((c > 0 && !SC_BUF_LIT(a, &b, ",")) || !result_column(r, c, &name, &type)) {
                return false;
            }
            bool quote = false;
            for (size_t i = 0; i < name.len; i++) {
                uint8_t ch = 0;
                if (!sc_slice_get(r->col_arena, name, i, &ch)) {
                    return false;
                }
                quote = quote || ch == ',' || ch == '"' || ch == '\n' || ch == '\r';
            }
            if (!copy_field(a, &b, r->col_arena, name, false, !quote, true)) {
                return false;
            }
        }
        if (!SC_BUF_LIT(a, &b, "\n")) {
            return false;
        }
    }
    result_cursor cur;
    if (!result_seek(&cur, r, first)) {
        return false;
    }
    uint64_t count = 0;
    for (uint64_t row = first; row <= last; row++) {
        if (row > first && (!SC_BUF_LIT(a, &b, "\n") || !result_next_row(&cur))) {
            return false;
        }
        for (uint64_t col = 0; col < r->cols; col++) {
            proto_cell cell;
            sc_str name;
            result_type type;
            if ((col > 0 && !sc_buf_lit(a, &b, (const uint8_t *)(csv ? "," : "\t"), 1)) ||
                !result_next_cell(&cur, &cell) || !result_column(r, col, &name, &type) ||
                !copy_field(a, &b, cur.rd.arena, cell.bytes, cell.null, type.numeric, csv)) {
                return false;
            }
            count += cell.truncated ? 1u : 0u;
        }
    }
    *out = sc_buf_view(a, b);
    *truncated = count;
    return true;
}
