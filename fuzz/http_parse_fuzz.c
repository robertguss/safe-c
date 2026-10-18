#include "../examples/http/parse.c"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

/* Counts CRLF pairs; on a 200 every line break in the head is one of them. */
static size_t count_crlf(const uint8_t *p, size_t n) {
    size_t c = 0;
    for (size_t i = 0; i + 1 < n; i++) {
        if (p[i] == '\r' && p[i + 1] == '\n')
            c++;
    }
    return c;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    alignas(8) uint8_t storage[32768];
    sc_arena a;
    sc_arena_init(&a, storage, sizeof storage);
    if (size > HTTP_MAX_HEAD + 64)
        size = HTTP_MAX_HEAD + 64;
    /* An odd first byte wraps the rest in a valid request line and a blank line,
     * so the mutator reaches the header parser instead of dying on the first line. */
    uint8_t framed[HTTP_MAX_HEAD + 128];
    if (size > 0 && (data[0] & 1)) {
        static const char start[] = "GET / HTTP/1.1\r\n";
        memcpy(framed, start, sizeof start - 1);
        memcpy(framed + sizeof start - 1, data + 1, size - 1);
        memcpy(framed + sizeof start - 1 + size - 1, "\r\n", 2);
        data = framed;
        size += sizeof start - 1 + 1;
    }
    sc_str head = sc_str_from_lit(&a, data, size);
    assert(head.len == size);

    http_request r;
    memset(&r, 0xa5, sizeof r);
    http_request before = r;
    unsigned st = http_parse(&a, head, &r);
    assert(st == 200 || st == 400 || st == 414 || st == 431);
    if (size > HTTP_MAX_HEAD)
        assert(st == 431);
    if (st != 200) {
        assert(memcmp(&r, &before, sizeof r) == 0);
    } else {
        assert(size >= 4 && memcmp(data + size - 4, "\r\n\r\n", 4) == 0);
        assert(r.header_count <= HTTP_MAX_HEADERS);
        assert(count_crlf(data, size) == r.header_count + 2);
        assert(r.method.len > 0 && r.target.len > 0 && r.version.len == 8);
        assert(r.target.ptr[0] == '/');
        assert(r.method.ptr >= head.ptr && r.method.ptr + r.method.len <= head.ptr + head.len);
        assert(r.headers.len == 0 || (r.headers.ptr >= head.ptr && r.headers.ptr + r.headers.len <= head.ptr + head.len));
        sc_str name = sc_str_from_lit(&a, (const uint8_t *)"host", 4);
        sc_str value = {NULL, 7, 7};
        if (http_header(&a, &r, name, &value))
            assert(value.ptr >= head.ptr && value.ptr + value.len <= head.ptr + head.len);
        else
            assert(value.len == 7);
    }

    /* once the arena is reset, the same head is stale and never parses */
    assert(sc_arena_reset(&a));
    http_request after = before;
    assert(http_parse(&a, head, &after) != 200);
    return 0;
}
