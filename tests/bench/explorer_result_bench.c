/* Times the explorer's result model on one million rows of one int column,
 * as `SELECT g FROM generate_series(1, 1000000)` streams them: indexing each
 * chunk as it arrives, result_find, and a seek plus a screenful of rows read
 * with the cursor. Built by `make bench` with -O2 and no sanitizers; the
 * numbers are recorded in examples/explorer/BREAKAGE.md, not gated. */
#define _POSIX_C_SOURCE 199309L
#include "../../examples/explorer/proto.c"
#include "../../examples/explorer/result.c"
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ROWS 1000000u
#define CHUNK_BYTES (256u * 1024u)
#define MAX_CHUNKS 128u
#define LOOKUPS 1000000u
#define SCREENS 20000u
#define SCREEN_ROWS 40u

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

int main(void) {
    uint8_t *mem = malloc((size_t)(MAX_CHUNKS + 1u) * CHUNK_BYTES);
    static sc_arena pool[MAX_CHUNKS + 1u];
    static alignas(8) uint8_t book_mem[1u << 20];
    static alignas(8) uint8_t tmp_mem[64];
    sc_arena book, tmp;
    if (mem == NULL) {
        return 1;
    }
    sc_arena_init(&book, book_mem, sizeof book_mem);
    result r;
    if (!result_init(&r, &book, MAX_CHUNKS)) {
        return 1;
    }
    sc_arena_init(&pool[MAX_CHUNKS], mem + (size_t)MAX_CHUNKS * CHUNK_BYTES, CHUNK_BYTES);
    proto_writer w;
    proto_header h = {PROTO_COLUMNS, 1, 1, 0, 0, 0};
    sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
    sc_str g = sc_str_from_lit(&tmp, (const uint8_t *)"g", 1);
    if (!proto_begin(&w, &pool[MAX_CHUNKS], h) || !proto_put_u64(&w, 1) ||
        !proto_put_str(&w, &tmp, g) || !proto_put_u64(&w, 23) || !proto_end(&w) ||
        result_set_columns(&r, &pool[MAX_CHUNKS], MAX_CHUNKS) != RESULT_ADDED) {
        return 1;
    }
    /* Fill chunks as the worker does: as many rows as fit. */
    double index_ns = 0;
    uint64_t next = 0;
    for (size_t k = 0; next < ROWS; k++) {
        if (k == MAX_CHUNKS) {
            return 1;
        }
        sc_arena_init(&pool[k], mem + k * CHUNK_BYTES, CHUNK_BYTES);
        uint64_t rows = 0;
        size_t body = 0;
        h.kind = PROTO_CHUNK;
        h.arg = 0;
        if (!proto_begin(&w, &pool[k], h)) {
            return 1;
        }
        while (next < ROWS && body + 40u <= CHUNK_BYTES - PROTO_HEADER_BYTES) {
            char num[24];
            int n = snprintf(num, sizeof num, "%llu", (unsigned long long)(next + 1u));
            sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
            if (!proto_put_u64(&w, (uint64_t)n) ||
                !proto_put_str(&w, &tmp, sc_str_from_lit(&tmp, (const uint8_t *)num, (size_t)n))) {
                return 1;
            }
            body += 16u + (size_t)n;
            next++;
            rows++;
        }
        w.h.arg = rows;
        double t0 = now_ns();
        if (!proto_end(&w) || result_add_chunk(&r, &pool[k], k) != RESULT_ADDED) {
            return 1;
        }
        index_ns += now_ns() - t0;
    }
    volatile uint64_t sink = 0;
    double t0 = now_ns();
    for (uint64_t i = 0; i < LOOKUPS; i++) {
        size_t ch = 0;
        uint64_t off = 0;
        uint64_t row = (i * 7919u) % ROWS;
        sink += result_find(&r, row, &ch, &off) ? off + ch : 0;
    }
    double find_ns = (now_ns() - t0) / LOOKUPS;
    t0 = now_ns();
    for (uint64_t i = 0; i < SCREENS; i++) {
        result_cursor c;
        uint64_t row = (i * 104729u) % (ROWS - SCREEN_ROWS);
        if (!result_seek(&c, &r, row)) {
            return 1;
        }
        for (uint64_t k = 0; k < SCREEN_ROWS; k++) {
            proto_cell cell;
            if ((k > 0 && !result_next_row(&c)) || !result_next_cell(&c, &cell)) {
                return 1;
            }
            sink += cell.bytes.len;
        }
    }
    double screen_ns = (now_ns() - t0) / SCREENS;
    printf("explorer_result_bench: %u rows in %zu chunks; indexing %.1f ms in all; "
           "result_find %.0f ns; seek + %u rows %.1f us (sink %llu)\n",
           ROWS, r.count, index_ns / 1e6, find_ns, SCREEN_ROWS, screen_ns / 1e3,
           (unsigned long long)sink);
    free(mem);
    return 0;
}
