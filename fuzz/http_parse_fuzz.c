#include "../examples/http/parse.c"
#include <assert.h>
#include <stdalign.h>
#include <strings.h>
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


/* Reference parser, written straight from the contract in parse.h with libc and
 * no arena, so a disagreement means one of the two is wrong. It returns the
 * status, and for 200 the header count and the first Host value. */
enum { REF_OK, REF_BAD, REF_LONG, REF_PARTIAL };

static int ref_line(const uint8_t *d, size_t n, size_t pos, size_t *len) {
    size_t k = pos;
    while (k < n && d[k] != '\r' && d[k] != '\n')
        k++;
    if (k - pos > HTTP_MAX_LINE)
        return REF_LONG;
    if (k == n)
        return REF_PARTIAL;
    if (d[k] == '\n')
        return REF_BAD;
    if (k + 1 >= n)
        return REF_PARTIAL;
    if (d[k + 1] != '\n')
        return REF_BAD;
    *len = k - pos;
    return REF_OK;
}

static int ref_token(uint8_t b) {
    return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') ||
           (b != 0 && strchr("!#$%&'*+-.^_`|~", b) != NULL);
}

static unsigned ref_status(const uint8_t *d, size_t n, size_t *count, const uint8_t **host,
                           size_t *host_len) {
    *count = 0;
    *host = NULL;
    *host_len = 0;
    if (n > HTTP_MAX_HEAD)
        return 431;
    size_t len = 0;
    int st = ref_line(d, n, 0, &len);
    if (st == REF_PARTIAL)
        return n >= HTTP_MAX_HEAD ? 431 : 400;
    if (st != REF_OK)
        return st == REF_LONG ? 414 : 400;
    const uint8_t *sp1 = memchr(d, ' ', len);
    if (!sp1)
        return 400;
    const uint8_t *target = sp1 + 1;
    const uint8_t *sp2 = memchr(target, ' ', (size_t)(d + len - target));
    if (!sp2)
        return 400;
    const uint8_t *version = sp2 + 1;
    size_t vlen = (size_t)(d + len - version);
    if (sp1 == d || sp2 == target || *target != '/')
        return 400;
    for (const uint8_t *p = d; p < sp1; p++) {
        if (*p < 'A' || *p > 'Z')
            return 400;
    }
    for (const uint8_t *p = target; p < sp2; p++) {
        if (*p <= 0x20 || *p == 0x7f)
            return 400;
    }
    if (!(vlen == 8 && (memcmp(version, "HTTP/1.1", 8) == 0 || memcmp(version, "HTTP/1.0", 8) == 0)))
        return 400;
    size_t pos = len + 2;
    for (;;) {
        st = ref_line(d, n, pos, &len);
        if (st == REF_PARTIAL)
            return n >= HTTP_MAX_HEAD ? 431 : 400;
        if (st != REF_OK)
            return st == REF_LONG ? 431 : 400;
        if (len == 0)
            break;
        const uint8_t *line = d + pos;
        const uint8_t *colon = memchr(line, ':', len);
        if (!colon || colon == line)
            return 400;
        for (const uint8_t *p = line; p < colon; p++) {
            if (!ref_token(*p))
                return 400;
        }
        const uint8_t *v = colon + 1;
        const uint8_t *end = line + len;
        while (v < end && (*v == ' ' || *v == '\t'))
            v++;
        while (end > v && (end[-1] == ' ' || end[-1] == '\t'))
            end--;
        for (const uint8_t *p = v; p < end; p++) {
            if ((*p < 0x20 && *p != '\t') || *p == 0x7f)
                return 400;
        }
        if (++*count > HTTP_MAX_HEADERS)
            return 431;
        if (!*host && (size_t)(colon - line) == 4 && strncasecmp((const char *)line, "host", 4) == 0) {
            *host = v;
            *host_len = (size_t)(end - v);
        }
        pos += len + 2;
    }
    return pos + 2 == n ? 200 : 400;
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
    size_t want_count = 0, want_host_len = 0;
    const uint8_t *want_host = NULL;
    assert(st == ref_status(data, size, &want_count, &want_host, &want_host_len));
    if (st != 200) {
        assert(memcmp(&r, &before, sizeof r) == 0);
    } else {
        assert(size >= 4 && memcmp(data + size - 4, "\r\n\r\n", 4) == 0);
        assert(r.header_count <= HTTP_MAX_HEADERS);
        assert(count_crlf(data, size) == r.header_count + 2);
        assert(r.header_count == want_count);
        assert(r.method.len > 0 && r.target.len > 0 && r.version.len == 8);
        assert(r.target.ptr[0] == '/');
        assert(r.method.ptr >= head.ptr && r.method.ptr + r.method.len <= head.ptr + head.len);
        assert(r.headers.len == 0 || (r.headers.ptr >= head.ptr && r.headers.ptr + r.headers.len <= head.ptr + head.len));
        sc_str name = sc_str_from_lit(&a, (const uint8_t *)"host", 4);
        sc_str value = {NULL, 7, 7};
        if (http_header(&a, &r, name, &value)) {
            assert(want_host != NULL && value.len == want_host_len);
            assert(value.len == 0 || memcmp(value.ptr, want_host, value.len) == 0);
        } else {
            assert(want_host == NULL && value.len == 7);
        }
    }

    /* once the arena is reset, the same head is stale and never parses */
    assert(sc_arena_reset(&a));
    http_request after = before;
    assert(http_parse(&a, head, &after) != 200);
    return 0;
}
