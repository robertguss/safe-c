/* Tests the HTTP demo's request parser (examples/http/parse.c). The parser is
 * application code, not stdlib, so it is built into this test directly. */
#include "../examples/http/parse.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

#define ARENA_BYTES (1u << 20)

static alignas(8) uint8_t storage[ARENA_BYTES];
static sc_arena a;

static sc_str head_of(const char *text) {
    return sc_str_from_lit(&a, (const uint8_t *)text, strlen(text));
}

static unsigned parse(const char *text, http_request *r) {
    return http_parse(&a, head_of(text), r);
}

static bool value_is(sc_str v, const char *want) {
    return v.len == strlen(want) && (v.len == 0 || memcmp(v.ptr, want, v.len) == 0);
}


/* head = prefix + filler + suffix, copied into one slice of the arena. */
static sc_str glued(const char *prefix, size_t filler, const char *suffix) {
    size_t p = strlen(prefix), q = strlen(suffix);
    sc_slice s = sc_arena_alloc(&a, p + filler + q);
    assert(s.ptr != NULL);
    memcpy(s.ptr, prefix, p);
    memset(s.ptr + p, 'a', filler);
    memcpy(s.ptr + p + filler, suffix, q);
    return s;
}

int main(void) {
    sc_arena_init(&a, storage, sizeof storage);
    http_request r;
    sc_str v;

    /* a valid request: fields, headers, lookup ignoring case, trimmed values */
    assert(parse("GET / HTTP/1.1\r\nHost: localhost:8080\r\nuser-AGENT:\t curl/8 \t\r\nX-Empty:\r\n\r\n", &r) == 200);
    assert(value_is(r.method, "GET") && value_is(r.target, "/") && value_is(r.version, "HTTP/1.1"));
    assert(r.header_count == 3);
    assert(http_header(&a, &r, SC_STR_LIT(&a, "host"), &v) && value_is(v, "localhost:8080"));
    assert(http_header(&a, &r, SC_STR_LIT(&a, "User-Agent"), &v) && value_is(v, "curl/8"));
    assert(http_header(&a, &r, SC_STR_LIT(&a, "X-EMPTY"), &v) && v.len == 0);
    v = SC_STR_LIT(&a, "keep");
    sc_str missing = SC_STR_LIT(&a, "accept");
    assert(!http_header(&a, &r, missing, &v) && value_is(v, "keep"));
    assert(!http_header(&a, &r, SC_STR_LIT(&a, "hos"), &v));
    assert(!http_header(&a, &r, SC_STR_LIT(&a, "hostx"), &v));

    /* first of repeated headers wins; no headers at all; HTTP/1.0; odd targets */
    assert(parse("GET /a?b=1 HTTP/1.0\r\nA: 1\r\nA: 2\r\n\r\n", &r) == 200);
    assert(value_is(r.target, "/a?b=1") && value_is(r.version, "HTTP/1.0"));
    assert(http_header(&a, &r, SC_STR_LIT(&a, "a"), &v) && value_is(v, "1"));
    assert(parse("POST /x HTTP/1.1\r\n\r\n", &r) == 200);
    assert(r.header_count == 0 && r.headers.len == 0);
    assert(!http_header(&a, &r, SC_STR_LIT(&a, "host"), &v));

    /* 400: malformed request line */
    const char *bad_lines[] = {
        "",
        "\r\n",
        "GET /\r\n\r\n",
        "GET / HTTP/1.1 extra\r\n\r\n",
        "GET  / HTTP/1.1\r\n\r\n",
        " GET / HTTP/1.1\r\n\r\n",
        "get / HTTP/1.1\r\n\r\n",
        "G3T / HTTP/1.1\r\n\r\n",
        "GET index.html HTTP/1.1\r\n\r\n",
        "GET /a\x01 HTTP/1.1\r\n\r\n",
        "GET / HTTP/2\r\n\r\n",
        "GET / http/1.1\r\n\r\n",
        "GET / HTTP/1.11\r\n\r\n",
        "GET / HTTP/1.1\n\r\n",
        "GET / HTTP/1.1\rHost: x\r\n\r\n",
    };
    for (size_t i = 0; i < sizeof bad_lines / sizeof bad_lines[0]; i++) {
        memset(&r, 0x5a, sizeof r);
        http_request before = r;
        assert(parse(bad_lines[i], &r) == 400);
        assert(memcmp(&r, &before, sizeof r) == 0);
    }

    /* 400: malformed headers and framing */
    const char *bad_heads[] = {
        "GET / HTTP/1.1\r\nHost: x\r\n",
        "GET / HTTP/1.1\r\nHost: x",
        "GET / HTTP/1.1\r\nHost x\r\n\r\n",
        "GET / HTTP/1.1\r\n: x\r\n\r\n",
        "GET / HTTP/1.1\r\nHost : x\r\n\r\n",
        "GET / HTTP/1.1\r\n Host: x\r\n\r\n",
        "GET / HTTP/1.1\r\nBad Name: x\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: a\x01z\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: a\nb\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: a\rb\r\n\r\n",
        "GET / HTTP/1.1\r\n\r\nGET / HTTP/1.1\r\n\r\n",
        "GET / HTTP/1.1\r\n\r\nx",
    };
    for (size_t i = 0; i < sizeof bad_heads / sizeof bad_heads[0]; i++)
        assert(parse(bad_heads[i], &r) == 400);

    /* 414 and 431: limits, each exactly at and one past */
    assert(http_parse(&a, glued("GET /", HTTP_MAX_LINE - 5 - 9, " HTTP/1.1\r\n\r\n"), &r) == 200);
    assert(http_parse(&a, glued("GET /", HTTP_MAX_LINE - 5 - 9 + 1, " HTTP/1.1\r\n\r\n"), &r) == 414);
    assert(http_parse(&a, glued("GET / HTTP/1.1\r\nX: ", HTTP_MAX_LINE - 3, "\r\n\r\n"), &r) == 200);
    assert(http_parse(&a, glued("GET / HTTP/1.1\r\nX: ", HTTP_MAX_LINE - 3 + 1, "\r\n\r\n"), &r) == 431);
    assert(http_parse(&a, glued("GET / HTTP/1.1\r\nX: ", 100000, "\r\n\r\n"), &r) == 431);
    /* an unterminated line is 431 (414 for the request line) once it passes the
     * limit, and only incomplete (400) up to it */
    assert(http_parse(&a, glued("GET / HTTP/1.1\r\nX: ", HTTP_MAX_LINE - 3, ""), &r) == 400);
    assert(http_parse(&a, glued("GET / HTTP/1.1\r\nX: ", HTTP_MAX_LINE - 3 + 1, ""), &r) == 431);
    assert(http_parse(&a, glued("GET /", HTTP_MAX_LINE - 5, ""), &r) == 400);
    assert(http_parse(&a, glued("GET /", HTTP_MAX_LINE - 5 + 1, ""), &r) == 414);
    assert(http_parse(&a, glued("GET / HTTP/1.1\r\nX: ", HTTP_MAX_LINE + 5, ""), &r) == 431);

    char many[16 * 1024];
    size_t used = (size_t)snprintf(many, sizeof many, "GET / HTTP/1.1\r\n");
    for (size_t i = 0; i < HTTP_MAX_HEADERS; i++)
        used += (size_t)snprintf(many + used, sizeof many - used, "H%zu: v\r\n", i);
    snprintf(many + used, sizeof many - used, "\r\n");
    assert(parse(many, &r) == 200 && r.header_count == HTTP_MAX_HEADERS);
    snprintf(many + used, sizeof many - used, "Extra: v\r\n\r\n");
    assert(parse(many, &r) == 431);

    /* whole head: exactly the limit passes, one over is 431, and an unfinished
     * head of the limit's size is 431 where a short unfinished one is 400 */
    /* exactly HTTP_MAX_HEAD bytes: request line (16) + 3 lines of 1000 + one filler line */
    used = (size_t)snprintf(many, sizeof many, "GET / HTTP/1.1\r\n");
    for (int i = 0; i < 3; i++) {
        used += (size_t)snprintf(many + used, sizeof many - used, "H%d: ", i);
        memset(many + used, 'v', 992);
        used += 992;
        used += (size_t)snprintf(many + used, sizeof many - used, "\r\n");
    }
    for (int i = 3; i < 8; i++) {
        used += (size_t)snprintf(many + used, sizeof many - used, "H%d: ", i);
        memset(many + used, 'v', 1000);
        used += 1000;
        used += (size_t)snprintf(many + used, sizeof many - used, "\r\n");
    }
    assert(used + 2 <= HTTP_MAX_HEAD);
    size_t pad = HTTP_MAX_HEAD - 2 - used;
    assert(pad >= 6);
    used += (size_t)snprintf(many + used, sizeof many - used, "P: ");
    memset(many + used, 'p', pad - 5);
    used += pad - 5;
    snprintf(many + used, sizeof many - used, "\r\n\r\n");
    assert(strlen(many) == HTTP_MAX_HEAD);
    assert(parse(many, &r) == 200);
    /* one byte over the limit is 431 even though every line is valid */
    sc_str exact = head_of(many);
    sc_slice padded = sc_arena_alloc(&a, HTTP_MAX_HEAD + 1);
    assert(padded.ptr != NULL);
    memcpy(padded.ptr, exact.ptr, used);
    padded.ptr[used] = 'p';
    memcpy(padded.ptr + used + 1, "\r\n\r\n", 4);
    assert(padded.len == HTTP_MAX_HEAD + 1 && http_parse(&a, padded, &r) == 431);

    /* unfinished at the limit is 431; the same head one byte short is 400 */
    many[HTTP_MAX_HEAD - 2] = 'p';
    many[HTTP_MAX_HEAD - 1] = 'p';
    assert(parse(many, &r) == 431);
    many[HTTP_MAX_HEAD - 2] = '\r';
    many[HTTP_MAX_HEAD - 1] = '\n';
    many[HTTP_MAX_HEAD - 1] = 0;
    assert(parse(many, &r) == 400);
    assert(http_parse(&a, glued("GET / HTTP/1.1\r\nA: ", 5, "\r\n\r\n"), &r) == 200);
    sc_str over = sc_slice_sub(&a, glued("GET / HTTP/1.1\r\n", HTTP_MAX_HEAD, ""), 0, HTTP_MAX_HEAD + 1);
    assert(over.len == HTTP_MAX_HEAD + 1 && http_parse(&a, over, &r) == 431);

    /* a stale head is malformed; an arena with no room for constants is 500 */
    sc_str h = head_of("GET / HTTP/1.1\r\n\r\n");
    assert(http_parse(&a, h, &r) == 200);
    assert(sc_arena_reset(&a));
    assert(http_parse(&a, h, &r) == 400);
    alignas(8) uint8_t tiny[8];
    sc_arena t;
    sc_arena_init(&t, tiny, sizeof tiny);
    assert(http_parse(&t, (sc_str){NULL, 0, 0}, &r) == 500);

    puts("http_parse_test ok");
    return 0;
}
