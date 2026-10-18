#include "parse.h"
#include "sc_microhttpd.h"

_Static_assert(HTTP_MAX_HEAD == SC_MHD_HEAD_LIMIT, "parser and adapter agree on the head limit");

static void reply_text(sc_http_reply *reply, unsigned status, sc_str body) {
    reply->status = status;
    reply->body = body;
}

/* Every request gets its own arena, so nothing here frees anything. */
static void handle(sc_arena *req, sc_str head, sc_http_reply *reply) {
    http_request r;
    unsigned status = http_parse(req, head, &r);
    if (status != 200) {
        reply_text(reply, status, SC_STR_LIT(req, "bad request\n"));
        return;
    }
    if (!sc_str_eq(req, r.target, SC_STR_LIT(req, "/"))) {
        reply_text(reply, 404, SC_STR_LIT(req, "not found\n"));
        return;
    }
    if (!sc_str_eq(req, r.method, SC_STR_LIT(req, "GET"))) {
        reply_text(reply, 405, SC_STR_LIT(req, "method not allowed\n"));
        return;
    }
    reply_text(reply, 200, SC_STR_LIT(req, "hello from safe c"));
}

int main(void) {
    return sc_mhd_serve(sc_mhd_port(8080), handle);
}
