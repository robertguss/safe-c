#include "history.h"

bool history_init(history *h, sc_arena *data, sc_arena *records) {
    history z = {0};
    *h = z;
    h->arena = data;
    h->records_arena = records;
    h->bytes = sc_arena_alloc(data, HISTORY_BYTES);
    h->records = SC_TSLICE_ALLOC(history_record, records, HISTORY_COUNT);
    return h->bytes.len == HISTORY_BYTES && sc_tslice_len(h->records) == HISTORY_COUNT;
}

bool history_get(const history *h, size_t i, history_entry *out) {
    history_record r;
    if (i >= h->count || !SC_TSLICE_GET(history_record, h->records_arena, h->records, i, &r)) return false;
    history_entry e = {sc_slice_sub(h->arena, h->bytes, r.offset, r.sql_len),
                       sc_slice_sub(h->arena, h->bytes, r.offset + r.sql_len, r.name_len), r.time};
    if (e.sql.len != r.sql_len || e.name.len != r.name_len) return false;
    *out = e;
    return true;
}

static bool history_drop(history *h) {
    history_record first;
    if (h->count == 0 || !SC_TSLICE_GET(history_record, h->records_arena, h->records, 0, &first)) return false;
    size_t removed = first.sql_len + first.name_len;
    if (!sc_slice_move(h->arena, sc_slice_sub(h->arena, h->bytes, 0, h->used - removed),
                       sc_slice_sub(h->arena, h->bytes, removed, h->used - removed))) return false;
    for (size_t i = 1; i < h->count; i++) {
        history_record r;
        if (!SC_TSLICE_GET(history_record, h->records_arena, h->records, i, &r)) return false;
        r.offset -= removed;
        if (!SC_TSLICE_SET(history_record, h->records_arena, h->records, i - 1u, &r)) return false;
    }
    h->count--;
    h->dropped++;
    h->used -= removed;
    return true;
}

bool history_add(history *h, const sc_arena *a, sc_str sql, const sc_arena *na, sc_str name, uint64_t time) {
    if (sql.len == 0 || sql.len > HISTORY_BYTES || name.len > HISTORY_BYTES - sql.len ||
        sql.epoch != a->epoch || name.epoch != na->epoch ||
        sc_slice_sub(a, sql, 0, sql.len).len != sql.len ||
        sc_slice_sub(na, name, 0, name.len).len != name.len) return false;
    history_entry last;
    if (h->count > 0 && history_get(h, h->count - 1u, &last) &&
        last.sql.len == sql.len && last.name.len == name.len) {
        bool equal = true;
        for (size_t i = 0; equal && i < sql.len; i++) {
            uint8_t x = 0, y = 0;
            equal = sc_slice_get(a, sql, i, &x) && sc_slice_get(h->arena, last.sql, i, &y) && x == y;
        }
        for (size_t i = 0; equal && i < name.len; i++) {
            uint8_t x = 0, y = 0;
            equal = sc_slice_get(na, name, i, &x) && sc_slice_get(h->arena, last.name, i, &y) && x == y;
        }
        if (equal) return true;
    }
    size_t n = sql.len + name.len;
    while (h->count == HISTORY_COUNT || n > HISTORY_BYTES - h->used) {
        if (!history_drop(h)) return false;
    }
    sc_buf b = sc_buf_new(sc_slice_sub(h->arena, h->bytes, h->used, n));
    history_record r = {h->used, sql.len, name.len, time};
    if (!sc_buf_str(h->arena, &b, a, sql) || !sc_buf_str(h->arena, &b, na, name) ||
        !SC_TSLICE_SET(history_record, h->records_arena, h->records, h->count, &r)) return false;
    h->count++;
    h->used += n;
    h->dirty = true;
    return true;
}

static bool history_number(const sc_arena *a, sc_str file, size_t *at, uint8_t end, uint64_t *out) {
    uint64_t n = 0;
    size_t start = *at;
    for (; *at < file.len; (*at)++) {
        uint8_t ch = 0;
        if (!sc_slice_get(a, file, *at, &ch)) return false;
        if (ch == end && *at > start) { (*at)++; *out = n; return true; }
        if (ch < '0' || ch > '9' || n > (UINT64_MAX - (ch - '0')) / 10u) return false;
        n = n * 10u + ch - '0';
    }
    return false;
}

bool history_load(history *h, const sc_arena *a, sc_str file) {
    h->count = h->used = h->dropped = 0;
    h->dirty = h->damaged = false;
    if (file.len == 0) return file.epoch == a->epoch;
    sc_str version = sc_slice_sub(a, file, 0, 17);
    if (!SC_STR_EQ_LIT(a, version, "SAFE-C-HISTORY 1\n")) { h->damaged = true; return true; }
    size_t at = 17;
    while (at < file.len) {
        uint8_t ch = 0;
        uint64_t len = 0, time = 0, name = 0;
        bool ok = sc_slice_get(a, file, at++, &ch) && ch == '@' &&
                  history_number(a, file, &at, ' ', &len) && history_number(a, file, &at, ' ', &time) &&
                  history_number(a, file, &at, '\n', &name) && len > 0 && len <= HISTORY_BYTES &&
                  name <= HISTORY_BYTES - len && len + name < file.len - at;
        if (!ok) { h->damaged = true; break; }
        sc_str title = sc_slice_sub(a, file, at, (size_t)name);
        sc_str sql = sc_slice_sub(a, file, at + (size_t)name, (size_t)len);
        at += (size_t)(name + len);
        if (!sc_slice_get(a, file, at++, &ch) || ch != '\n' ||
            !history_add(h, a, sql, a, title, time)) { h->damaged = true; break; }
    }
    h->dirty = false;
    return true;
}

bool history_write(const history *h, sc_arena *a, sc_str *out) {
    sc_buf b = sc_buf_new(sc_arena_alloc(a, HISTORY_FILE_BYTES));
    if (!SC_BUF_LIT(a, &b, "SAFE-C-HISTORY 1\n")) return false;
    for (size_t i = 0; i < h->count; i++) {
        history_entry e;
        if (!history_get(h, i, &e) || !SC_BUF_LIT(a, &b, "@") || !sc_buf_u64(a, &b, e.sql.len) ||
            !SC_BUF_LIT(a, &b, " ") || !sc_buf_u64(a, &b, e.time) || !SC_BUF_LIT(a, &b, " ") ||
            !sc_buf_u64(a, &b, e.name.len) || !SC_BUF_LIT(a, &b, "\n") ||
            !sc_buf_str(a, &b, h->arena, e.name) || !sc_buf_str(a, &b, h->arena, e.sql) ||
            !SC_BUF_LIT(a, &b, "\n")) return false;
    }
    *out = sc_buf_view(a, b);
    return true;
}
