/* Tests the explorer's message header and body fields
 * (examples/explorer/proto.c). Core code, built in. */
#include "../examples/explorer/proto.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

#define MSG_BYTES 4096u

static alignas(8) uint8_t msg_mem[MSG_BYTES];
static alignas(8) uint8_t src_mem[1024];
static sc_arena msg, src;

static proto_header hdr(proto_kind k, uint32_t conn, uint64_t req, uint64_t arg, uint64_t aux) {
    proto_header h = {k, conn, req, arg, aux, 0};
    return h;
}

/* Every kind round-trips with its fields, including values that use all 64
 * bits, and the body comes back field by field. */
static void round_trip_every_kind(void) {
    for (int k = PROTO_OPEN; k < PROTO_KIND_COUNT; k++) {
        uint64_t aux = (k == PROTO_CANCEL || k == PROTO_CAPPED)  ? (uint64_t)PROTO_CAP_MEMORY
                       : (k == PROTO_CONN_FAILED)                ? (uint64_t)PROTO_FAIL_AUTH
                                                                 : 0x8000000000000001u;
        sc_arena_init(&msg, msg_mem, sizeof msg_mem);
        sc_arena_init(&src, src_mem, sizeof src_mem);
        sc_str text = SC_STR_LIT(&src, "select 1");
        sc_str empty = {NULL, 0, src.epoch};
        proto_writer w;
        assert(proto_begin(&w, &msg, hdr((proto_kind)k, 0xfffffffeu, 0x0102030405060708u,
                                         UINT64_MAX, aux)));
        assert(proto_put_str(&w, &src, text));
        assert(proto_put_u64(&w, 42));
        assert(proto_put_str(&w, &src, empty));
        assert(proto_end(&w));

        proto_header h;
        proto_reader r;
        assert(proto_read(&msg, &h, &r));
        assert(h.kind == (proto_kind)k && h.conn == 0xfffffffeu && h.req == 0x0102030405060708u);
        assert(h.arg == UINT64_MAX && h.aux == aux);
        assert(h.body_len == 8 + 8 + 8 + 8);
        sc_str s;
        uint64_t v = 0;
        assert(proto_get_str(&r, &s) && sc_str_eq_lit(&msg, s, (const uint8_t *)"select 1", 8));
        assert(proto_get_u64(&r, &v) && v == 42);
        assert(proto_get_str(&r, &s) && s.len == 0);
        assert(!proto_get_u64(&r, &v) && v == 42); /* past the body */
        assert(proto_to_worker((proto_kind)k) == (k <= PROTO_RETURN_TO_WORKER));
    }
}

/* The header is little-endian at offset 0, whatever the host. */
static void header_layout(void) {
    sc_arena_init(&msg, msg_mem, sizeof msg_mem);
    proto_writer w;
    assert(proto_begin(&w, &msg, hdr(PROTO_CHUNK, 3, 0x1122, 7, 0)));
    assert(proto_end(&w));
    assert(msg_mem[0] == 0x53 && msg_mem[7] == 0x31); /* "S" ... "1" */
    assert(msg_mem[8] == PROTO_CHUNK && msg_mem[9] == 0);
    assert(msg_mem[16] == 3);
    assert(msg_mem[24] == 0x22 && msg_mem[25] == 0x11);
    assert(msg_mem[32] == 7);
    assert(msg_mem[48] == 0); /* empty body */
}

static void write_word(size_t at, uint64_t v) {
    for (unsigned i = 0; i < 8; i++) {
        msg_mem[at + i] = (uint8_t)(v >> (8 * i));
    }
}

/* A header claiming a body past the arena's used bytes is rejected; one that
 * ends exactly at them is not. So are a bad magic, kind and reason. */
static void rejects_bad_headers(void) {
    proto_header h, keep = hdr(PROTO_RUN, 9, 9, 9, 9);
    proto_reader r;
    sc_arena_init(&msg, msg_mem, sizeof msg_mem);
    proto_writer w;
    assert(proto_begin(&w, &msg, hdr(PROTO_DONE, 1, 1, 0, 0)));
    assert(proto_end(&w));
    size_t room = msg.used - PROTO_HEADER_BYTES;

    write_word(48, room);
    h = keep;
    assert(proto_read(&msg, &h, &r) && h.body_len == room);
    write_word(48, room + 1);
    h = keep;
    assert(!proto_read(&msg, &h, &r));
    assert(h.kind == keep.kind && h.req == keep.req); /* untouched */
    write_word(48, UINT64_MAX);
    assert(!proto_read(&msg, &h, &r));
    write_word(48, 0);
    assert(proto_read(&msg, &h, &r));

    write_word(8, PROTO_NONE);
    assert(!proto_read(&msg, &h, &r));
    write_word(8, PROTO_KIND_COUNT);
    assert(!proto_read(&msg, &h, &r));
    write_word(8, (uint64_t)PROTO_CAPPED);
    write_word(40, PROTO_CAP_COUNT);
    assert(!proto_read(&msg, &h, &r));
    write_word(40, PROTO_CAP_COUNT - 1);
    assert(proto_read(&msg, &h, &r));
    write_word(8, (uint64_t)PROTO_CONN_FAILED);
    write_word(40, PROTO_FAIL_COUNT);
    assert(!proto_read(&msg, &h, &r));
    write_word(40, 0);
    write_word(16, (uint64_t)UINT32_MAX + 1);
    assert(!proto_read(&msg, &h, &r));
    write_word(16, 1);
    msg_mem[0] ^= 1;
    assert(!proto_read(&msg, &h, &r));
    msg_mem[0] ^= 1;
    assert(proto_read(&msg, &h, &r));

    /* An arena holding fewer bytes than a header, or a stale one. */
    sc_arena small;
    sc_arena_init(&small, msg_mem, 48);
    assert(!proto_read(&small, &h, &r));
    assert(sc_arena_reset(&msg));
    assert(!proto_read(&msg, &h, &r));
}

/* A string field whose length runs past the body is rejected and the reader
 * stays failed. */
static void rejects_bad_fields(void) {
    sc_arena_init(&msg, msg_mem, sizeof msg_mem);
    proto_writer w;
    assert(proto_begin(&w, &msg, hdr(PROTO_ERROR, 1, 1, 0, 0)));
    assert(proto_put_u64(&w, 5)); /* read back as a string length of 5 */
    assert(proto_put_u64(&w, 0));
    assert(proto_end(&w));
    proto_header h;
    proto_reader r;
    assert(proto_read(&msg, &h, &r));
    sc_str s = {NULL, 0, 0};
    assert(proto_get_str(&r, &s) && s.len == 5);
    assert(!proto_get_str(&r, &s) && s.len == 5); /* 3 bytes left */
    uint64_t v = 1;
    assert(!proto_get_u64(&r, &v) && v == 1); /* failure sticks */

    write_word(PROTO_HEADER_BYTES, 9); /* exactly one past the 8 bytes after it */
    assert(proto_read(&msg, &h, &r));
    assert(!proto_get_str(&r, &s));
    write_word(PROTO_HEADER_BYTES, 8);
    assert(proto_read(&msg, &h, &r));
    assert(proto_get_str(&r, &s) && s.len == 8);
}

/* The writer refuses a used arena, a bad kind, an arena too small for the
 * header, and a body that does not fit; the failure sticks. */
static void writer_limits(void) {
    proto_writer w;
    sc_arena_init(&msg, msg_mem, sizeof msg_mem);
    assert(!proto_begin(&w, &msg, hdr(PROTO_NONE, 0, 0, 0, 0)));
    assert(!proto_begin(&w, &msg, hdr(PROTO_KIND_COUNT, 0, 0, 0, 0)));
    (void)sc_arena_alloc(&msg, 8);
    assert(!proto_begin(&w, &msg, hdr(PROTO_DONE, 0, 0, 0, 0)));
    sc_arena tiny;
    sc_arena_init(&tiny, msg_mem, PROTO_HEADER_BYTES);
    assert(!proto_begin(&w, &tiny, hdr(PROTO_DONE, 0, 0, 0, 0)));
    sc_arena_init(&tiny, msg_mem, PROTO_HEADER_BYTES + 8);
    assert(proto_begin(&w, &tiny, hdr(PROTO_DONE, 0, 0, 0, 0)));
    assert(proto_put_u64(&w, 1));
    assert(!proto_put_u64(&w, 2));
    assert(!proto_end(&w));

    /* A body handed out and back may grow, but not be swapped. */
    sc_arena_init(&msg, msg_mem, sizeof msg_mem);
    sc_arena_init(&src, src_mem, sizeof src_mem);
    assert(proto_begin(&w, &msg, hdr(PROTO_CHUNK, 0, 0, 0, 0)));
    sc_buf b = proto_body(&w);
    assert(SC_BUF_LIT(&msg, &b, "rows"));
    assert(proto_set_body(&w, b));
    sc_buf other = sc_buf_new(sc_arena_alloc(&src, 64));
    assert(!proto_set_body(&w, other));
    assert(!proto_end(&w));
    assert(proto_begin(&w, &msg, hdr(PROTO_CHUNK, 0, 0, 0, 0)) == false); /* used */
    assert(sc_arena_reset(&msg));
    assert(proto_begin(&w, &msg, hdr(PROTO_CHUNK, 0, 0, 0, 0)));
    b = proto_body(&w);
    assert(SC_BUF_LIT(&msg, &b, "rows"));
    assert(proto_set_body(&w, b));
    assert(proto_end(&w));
    proto_header h;
    proto_reader r;
    assert(proto_read(&msg, &h, &r) && h.body_len == 4);
}

int main(void) {
    round_trip_every_kind();
    header_layout();
    rejects_bad_headers();
    rejects_bad_fields();
    writer_limits();
    puts("explorer_proto_test: ok");
    return 0;
}
