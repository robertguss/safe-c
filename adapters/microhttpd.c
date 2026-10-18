/* libmicrohttpd adapter for the HTTP demo (subset rule R0). It owns the C-string
 * world: it rebuilds each request head as bytes in a per-request arena, calls the
 * handler, copies the reply out, and resets the arena once the response is done. */
#define _POSIX_C_SOURCE 200809L
#include "sc_microhttpd.h"
#include <microhttpd.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS 64
#define REQUEST_ARENA 16384

typedef struct {
    sc_arena arena;
    uint8_t *store;
    int busy;
} slot;

static slot pool[SLOTS];
static sc_http_handler handler_fn;

static slot *slot_acquire(void) {
    for (size_t i = 0; i < SLOTS; i++) {
        if (!pool[i].busy) {
            pool[i].busy = 1;
            return &pool[i];
        }
    }
    return NULL;
}

/* Reset after the response. The epoch counter wraps only after UINT_MAX requests
 * on one slot; no slice outlives its request, so restarting it is safe. */
static void slot_release(slot *s) {
    if (!sc_arena_reset(&s->arena))
        sc_arena_init(&s->arena, s->store, REQUEST_ARENA);
    s->busy = 0;
}

typedef struct {
    uint8_t *p;
    size_t len;
    size_t cap;
} builder;

/* Appends up to the builder's capacity; a full builder stays full. */
static void put(builder *b, const void *src, size_t n) {
    size_t room = b->cap - b->len;
    size_t take = n < room ? n : room;
    memcpy(b->p + b->len, src, take);
    b->len += take;
}

static void put_cstr(builder *b, const char *s) {
    put(b, s, strlen(s));
}

static enum MHD_Result add_header(void *cls, enum MHD_ValueKind kind, const char *key,
                                  const char *value) {
    (void)kind;
    builder *b = cls;
    put_cstr(b, key);
    put_cstr(b, ": ");
    put_cstr(b, value ? value : "");
    put_cstr(b, "\r\n");
    return MHD_YES;
}

static enum MHD_Result respond(struct MHD_Connection *c, unsigned status, const void *body,
                               size_t n, enum MHD_ResponseMemoryMode mode) {
    struct MHD_Response *r = MHD_create_response_from_buffer(n, (void *)body, mode);
    if (!r)
        return MHD_NO;
    MHD_add_response_header(r, "Content-Type", "text/plain");
    enum MHD_Result ret = MHD_queue_response(c, status, r);
    MHD_destroy_response(r);
    return ret;
}

static enum MHD_Result on_request(void *cls, struct MHD_Connection *c, const char *url,
                                  const char *method, const char *version, const char *upload,
                                  size_t *upload_size, void **req_cls) {
    (void)cls;
    (void)upload;
    (void)upload_size;
    if (*req_cls)
        return MHD_YES;
    slot *s = slot_acquire();
    if (!s)
        return respond(c, 503, "busy\n", 5, MHD_RESPMEM_PERSISTENT);
    *req_cls = s;

    sc_slice head_buf = sc_arena_alloc(&s->arena, SC_MHD_HEAD_LIMIT + 1);
    if (!head_buf.ptr)
        return respond(c, 500, "no memory\n", 10, MHD_RESPMEM_PERSISTENT);
    builder b = {head_buf.ptr, 0, head_buf.len};
    put_cstr(&b, method);
    put_cstr(&b, " ");
    put_cstr(&b, url);
    put_cstr(&b, " ");
    put_cstr(&b, version);
    put_cstr(&b, "\r\n");
    MHD_get_connection_values(c, MHD_HEADER_KIND, add_header, &b);
    put_cstr(&b, "\r\n");
    sc_str head = {head_buf.ptr, b.len, head_buf.epoch};

    sc_http_reply reply = {500, {NULL, 0, 0}};
    handler_fn(&s->arena, head, &reply);
    if (reply.body.len > 0 && reply.body.epoch != s->arena.epoch)
        return respond(c, 500, "stale reply\n", 12, MHD_RESPMEM_PERSISTENT);
    return respond(c, reply.status, reply.body.ptr, reply.body.len, MHD_RESPMEM_MUST_COPY);
}

static void on_done(void *cls, struct MHD_Connection *c, void **req_cls,
                    enum MHD_RequestTerminationCode toe) {
    (void)cls;
    (void)c;
    (void)toe;
    if (*req_cls) {
        slot_release(*req_cls);
        *req_cls = NULL;
    }
}

unsigned sc_mhd_port(unsigned fallback) {
    const char *env = getenv("SC_HTTP_PORT");
    if (!env)
        return fallback;
    char *end = NULL;
    unsigned long v = strtoul(env, &end, 10);
    return (*env && !*end && v > 0 && v < 65536) ? (unsigned)v : fallback;
}

int sc_mhd_serve(unsigned port, sc_http_handler handler) {
    handler_fn = handler;
    for (size_t i = 0; i < SLOTS; i++) {
        pool[i].store = malloc(REQUEST_ARENA);
        if (!pool[i].store)
            return 1;
        sc_arena_init(&pool[i].arena, pool[i].store, REQUEST_ARENA);
    }
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    struct MHD_Daemon *d = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD, (uint16_t)port, NULL, NULL, on_request, NULL,
        MHD_OPTION_NOTIFY_COMPLETED, on_done, NULL, MHD_OPTION_CONNECTION_TIMEOUT, 10u,
        MHD_OPTION_END);
    if (!d) {
        fprintf(stderr, "cannot listen on port %u\n", port);
        return 1;
    }
    fprintf(stderr, "listening on port %u\n", port);
    int sig = 0;
    sigwait(&set, &sig);
    MHD_stop_daemon(d);
    return 0;
}
