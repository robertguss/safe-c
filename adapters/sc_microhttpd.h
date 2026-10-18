#ifndef SAFEC_ADAPTER_MICROHTTPD_H
#define SAFEC_ADAPTER_MICROHTTPD_H

#include "safec/arena.h"
#include "safec/str.h"

/* The adapter hands the handler a request head of at most this many bytes. A
 * longer head arrives truncated to SC_MHD_HEAD_LIMIT + 1 bytes, so a handler
 * that rejects heads over the limit sees the overflow. */
#define SC_MHD_HEAD_LIMIT 8192

typedef struct {
    unsigned status;
    sc_str body; /* live in the request arena; the adapter copies it before the reset */
} sc_http_reply;

/* Called once per request on the server thread. `req` is that request's own
 * arena, empty on entry and reset by the adapter after the response is queued;
 * `head` is the request line and headers rebuilt as "METHOD target version",
 * "name: value" lines and a blank line, all CRLF, living in `req`. The handler
 * fills *reply. A body that is not live in `req` is answered with 500. */
typedef void (*sc_http_handler)(sc_arena *req, sc_str head, sc_http_reply *reply);

/* Port from $SC_HTTP_PORT, or `fallback` when it is unset or not a port. */
unsigned sc_mhd_port(unsigned fallback);

/* Serves on `port` with one polling thread, so requests run one at a time,
 * until SIGINT or SIGTERM. Returns 0 after a clean shutdown and 1 when the
 * server cannot start. */
int sc_mhd_serve(unsigned port, sc_http_handler handler);

#endif
