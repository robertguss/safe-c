#include "proto.h"

static bool put_word(const sc_arena *a, sc_buf *b, uint64_t v) {
    for (unsigned i = 0; i < 8u; i++) {
        uint8_t byte = (uint8_t)(v >> (8u * i));
        if (!sc_buf_lit(a, b, &byte, 1)) {
            return false;
        }
    }
    return true;
}

static bool get_word(const sc_arena *a, sc_slice s, size_t at, uint64_t *out) {
    uint64_t v = 0;
    if (at > s.len || s.len - at < 8u) {
        return false;
    }
    for (unsigned i = 0; i < 8u; i++) {
        uint8_t byte = 0;
        if (!sc_slice_get(a, s, at + i, &byte)) {
            return false;
        }
        v |= (uint64_t)byte << (8u * i);
    }
    *out = v;
    return true;
}

bool proto_to_worker(proto_kind k) {
    return k >= PROTO_OPEN && k <= PROTO_RETURN_TO_WORKER;
}

bool proto_begin(proto_writer *w, sc_arena *a, proto_header h) {
    w->arena = a;
    w->h = h;
    w->h.body_len = 0;
    w->failed = true;
    if (a->used != 0 || h.kind <= PROTO_NONE || h.kind >= PROTO_KIND_COUNT) {
        return false;
    }
    size_t n = a->cap / 8u * 8u;
    if (n <= PROTO_HEADER_BYTES) {
        return false;
    }
    sc_slice all = sc_arena_alloc(a, n);
    w->head = sc_slice_sub(a, all, 0, PROTO_HEADER_BYTES);
    sc_slice body = sc_slice_sub(a, all, PROTO_HEADER_BYTES, n - PROTO_HEADER_BYTES);
    if (w->head.len != PROTO_HEADER_BYTES || body.len != n - PROTO_HEADER_BYTES) {
        return false;
    }
    w->body = sc_buf_new(body);
    w->failed = false;
    return true;
}

static bool wfail(proto_writer *w) {
    w->failed = true;
    return false;
}

bool proto_put_u64(proto_writer *w, uint64_t v) {
    if (w->failed || !put_word(w->arena, &w->body, v)) {
        return wfail(w);
    }
    return true;
}

bool proto_put_str(proto_writer *w, const sc_arena *sa, sc_str s) {
    if (w->failed || !put_word(w->arena, &w->body, (uint64_t)s.len) ||
        !sc_buf_str(w->arena, &w->body, sa, s)) {
        return wfail(w);
    }
    return true;
}

sc_buf proto_body(const proto_writer *w) {
    return w->body;
}

bool proto_set_body(proto_writer *w, sc_buf b) {
    /* Only the same buffer, grown or overflowed, may come back. */
    if (w->failed || b.dst.ptr != w->body.dst.ptr || b.dst.len != w->body.dst.len ||
        b.dst.epoch != w->body.dst.epoch || b.len < w->body.len || b.len > b.dst.len) {
        return wfail(w);
    }
    w->body = b;
    if (b.overflow) {
        return wfail(w);
    }
    return true;
}

bool proto_end(proto_writer *w) {
    if (w->failed || w->body.overflow) {
        return wfail(w);
    }
    sc_buf hb = sc_buf_new(w->head);
    w->h.body_len = w->body.len;
    if (!put_word(w->arena, &hb, PROTO_MAGIC) || !put_word(w->arena, &hb, (uint64_t)w->h.kind) ||
        !put_word(w->arena, &hb, (uint64_t)w->h.conn) || !put_word(w->arena, &hb, w->h.req) ||
        !put_word(w->arena, &hb, w->h.arg) || !put_word(w->arena, &hb, w->h.aux) ||
        !put_word(w->arena, &hb, (uint64_t)w->h.body_len)) {
        return wfail(w);
    }
    return true;
}

static bool aux_ok(proto_kind k, uint64_t aux) {
    if (k == PROTO_CANCEL || k == PROTO_CAPPED) {
        return aux < (uint64_t)PROTO_CAP_COUNT;
    }
    if (k == PROTO_CONN_FAILED) {
        return aux < (uint64_t)PROTO_FAIL_COUNT;
    }
    return true;
}

bool proto_read(const sc_arena *a, proto_header *h, proto_reader *r) {
    /* The writer took the whole arena in one allocation at offset 0, so the
     * message is the arena's used bytes. No stdlib call hands out that view, so
     * it is spelled from the arena's own fields, which meets the slice contract
     * by construction; the reads below recheck only its epoch and bounds. */
    sc_slice used = {a->base, a->used, a->epoch};
    uint64_t magic = 0, kind = 0, conn = 0, body = 0;
    proto_header out;
    if (a->used < PROTO_HEADER_BYTES || !get_word(a, used, 0, &magic) || magic != PROTO_MAGIC ||
        !get_word(a, used, 8, &kind) || !get_word(a, used, 16, &conn) ||
        !get_word(a, used, 24, &out.req) || !get_word(a, used, 32, &out.arg) ||
        !get_word(a, used, 40, &out.aux) || !get_word(a, used, 48, &body)) {
        return false;
    }
    if (kind <= (uint64_t)PROTO_NONE || kind >= (uint64_t)PROTO_KIND_COUNT ||
        conn > (uint64_t)UINT32_MAX || body > (uint64_t)(a->used - PROTO_HEADER_BYTES)) {
        return false;
    }
    out.kind = (proto_kind)kind;
    out.conn = (uint32_t)conn;
    out.body_len = (size_t)body;
    if (!aux_ok(out.kind, out.aux)) {
        return false;
    }
    r->arena = a;
    r->pos = 0;
    r->failed = false;
    r->body.ptr = NULL;
    r->body.len = 0;
    r->body.epoch = a->epoch;
    if (out.body_len > 0) {
        r->body = sc_slice_sub(a, used, PROTO_HEADER_BYTES, out.body_len);
        if (r->body.len != out.body_len) {
            return false;
        }
    }
    *h = out;
    return true;
}

static bool rfail(proto_reader *r) {
    r->failed = true;
    return false;
}

bool proto_get_u64(proto_reader *r, uint64_t *out) {
    uint64_t v = 0;
    if (r->failed || !get_word(r->arena, r->body, r->pos, &v)) {
        return rfail(r);
    }
    r->pos += 8u;
    *out = v;
    return true;
}

bool proto_get_str(proto_reader *r, sc_str *out) {
    uint64_t n = 0;
    if (r->failed || !get_word(r->arena, r->body, r->pos, &n) ||
        n > (uint64_t)(r->body.len - r->pos - 8u)) {
        return rfail(r);
    }
    size_t at = r->pos + 8u;
    sc_str s = {NULL, 0, r->arena->epoch};
    if (n > 0) {
        s = sc_slice_sub(r->arena, r->body, at, (size_t)n);
        if (s.len != (size_t)n) {
            return rfail(r);
        }
    }
    r->pos = at + (size_t)n;
    *out = s;
    return true;
}

bool proto_get_cell(proto_reader *r, proto_cell *out) {
    uint64_t meta = 0;
    sc_str bytes;
    if (!proto_get_u64(r, &meta) || !proto_get_str(r, &bytes)) {
        return false;
    }
    proto_cell c;
    c.null = (meta & PROTO_CELL_NULL) != 0;
    c.truncated = (meta & PROTO_CELL_TRUNCATED) != 0;
    c.len = meta & ~(PROTO_CELL_NULL | PROTO_CELL_TRUNCATED);
    c.bytes = bytes;
    bool short_ok = c.truncated ? bytes.len < c.len : bytes.len == c.len;
    if (bytes.len > PROTO_CELL_CAP || !short_ok || (c.null && (c.len != 0 || c.truncated))) {
        return rfail(r);
    }
    *out = c;
    return true;
}
