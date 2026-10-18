#ifndef SAFEC_EXAMPLE_HTTP_PARSE_H
#define SAFEC_EXAMPLE_HTTP_PARSE_H

#include "safec/arena.h"
#include "safec/str.h"

/* Limits the parser enforces on a request head (request line plus headers,
 * through the blank line that ends them). */
#define HTTP_MAX_HEAD 8192
#define HTTP_MAX_LINE 4096
#define HTTP_MAX_HEADERS 64

/* Every field is a slice of the head it was parsed from, so it lives and dies
 * with the head's arena. `headers` is the validated block of "name: value"
 * lines; look one up with http_header. */
typedef struct {
    sc_str method;
    sc_str target;
    sc_str version;
    sc_str headers;
    size_t header_count;
} http_request;

/* Parses the head of an HTTP/1.x request and returns the status to answer
 * with: 200 when the head is well formed, 400 for a malformed or stale head
 * (bare CR or LF, bad method, target, version or header line, bytes after the
 * blank line, no blank line), 414 for a request line over HTTP_MAX_LINE, 431
 * for a header line over HTTP_MAX_LINE, more than HTTP_MAX_HEADERS headers, or
 * a head over HTTP_MAX_HEAD, and 500 when the arena has no room for the
 * parser's constants. *out changes only on 200. head must come from a. */
unsigned http_parse(sc_arena *a, sc_str head, http_request *out);

/* Finds the first header whose name equals `name` ignoring ASCII case and
 * writes its value, without the surrounding blanks, to *value. Returns false
 * and leaves *value alone when there is none. `name` and r must be live in a. */
bool http_header(const sc_arena *a, const http_request *r, sc_str name, sc_str *value);

#endif
