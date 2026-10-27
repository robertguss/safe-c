/* Fuzzes the explorer's thread protocol (examples/explorer/proto.c) and its two
 * connection machines (examples/explorer/conn.c, conn_worker.c).
 *
 * Even first byte: the rest is a message arena (with the magic and a kind in
 * range forced when the byte is 2 mod 4). proto_read must agree with a
 * reference decoder written from proto.h with plain loads, and no field it
 * hands out may run past the arena's used bytes.
 *
 * Odd first byte: the rest is a script. Each byte picks a step that the glue
 * could really take next: a user command, delivering one queued message, or a
 * libpq event that fits the worker's state, the end of a libpq 17+ cancel
 * among them. The simulator checks arena
 * ownership after every step, every step must succeed, and closing at the end
 * must bring every arena home and stop the worker. */
#include "../examples/explorer/proto.c"
#include "../examples/explorer/budget.c"
#include "../examples/explorer/conn.c"
#include "../examples/explorer/conn_worker.c"
#include "../tests/explorer_conn_sim.h"

#define FUZZ_ARENA 1024u

static alignas(8) uint8_t mem[FUZZ_ARENA];

static uint64_t ref_word(const uint8_t *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++) {
        v |= (uint64_t)p[i] << (8 * i);
    }
    return v;
}

static void put_ref_word(uint8_t *p, uint64_t v) {
    for (unsigned i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static void fuzz_header(const uint8_t *data, size_t size, bool force) {
    size_t n = size / 8 * 8;
    if (n > FUZZ_ARENA) {
        n = FUZZ_ARENA;
    }
    sc_arena a;
    sc_arena_init(&a, mem, FUZZ_ARENA);
    memset(mem, 0, sizeof mem);
    if (n > 0) {
        sc_slice s = sc_arena_alloc(&a, n);
        assert(s.len == n);
        memcpy(mem, data, n);
    }
    /* Half the inputs get a header that decodes (magic, kind, conn, reason and
     * a body length in range), so the body fields behind it are reached. */
    if (force && n >= PROTO_HEADER_BYTES) {
        put_ref_word(mem, PROTO_MAGIC);
        uint64_t k = 1 + mem[8] % (PROTO_KIND_COUNT - 1);
        put_ref_word(mem + 8, k);
        memset(mem + 20, 0, 4);
        if (k == PROTO_CANCEL || k == PROTO_CAPPED) {
            put_ref_word(mem + 40, ref_word(mem + 40) % PROTO_CAP_COUNT);
        } else if (k == PROTO_CONN_FAILED) {
            put_ref_word(mem + 40, ref_word(mem + 40) % PROTO_FAIL_COUNT);
        }
        put_ref_word(mem + 48, ref_word(mem + 48) % (n - PROTO_HEADER_BYTES + 1));
    }
    bool want = n >= PROTO_HEADER_BYTES && ref_word(mem) == PROTO_MAGIC;
    uint64_t kind = 0, aux = 0, body = 0;
    if (want) {
        kind = ref_word(mem + 8);
        aux = ref_word(mem + 40);
        body = ref_word(mem + 48);
        want = kind > PROTO_NONE && kind < PROTO_KIND_COUNT && ref_word(mem + 16) <= UINT32_MAX &&
               body <= n - PROTO_HEADER_BYTES;
        if (kind == PROTO_CANCEL || kind == PROTO_CAPPED) {
            want = want && aux < PROTO_CAP_COUNT;
        } else if (kind == PROTO_CONN_FAILED) {
            want = want && aux < PROTO_FAIL_COUNT;
        }
    }
    proto_header h;
    proto_reader r;
    bool got = proto_read(&a, &h, &r);
    assert(got == want);
    if (!got) {
        return;
    }
    assert((uint64_t)h.kind == kind && h.body_len == body && h.aux == aux);
    assert(h.req == ref_word(mem + 24) && h.arg == ref_word(mem + 32));
    /* Read the body as alternating string and u64 fields, each checked
     * against a walk of the raw bytes: a field reads exactly when it fits. */
    const uint8_t *bytes = mem + PROTO_HEADER_BYTES;
    size_t pos = 0;
    for (unsigned i = 0; i < 64; i++) {
        size_t left = h.body_len - pos;
        bool str = i % 2 == 0;
        uint64_t word = left >= 8 ? ref_word(bytes + pos) : 0;
        bool fits = left >= 8 && (!str || word <= left - 8);
        sc_str s = {NULL, 0, 0};
        uint64_t v = 0;
        bool got_field = str ? proto_get_str(&r, &s) : proto_get_u64(&r, &v);
        assert(got_field == fits);
        if (!fits) {
            assert(r.failed && !proto_get_u64(&r, &v)); /* the failure sticks */
            break;
        }
        if (str) {
            assert(s.len == word && (s.len == 0 || s.ptr == bytes + pos + 8));
            pos += 8 + (size_t)word;
        } else {
            assert(v == word);
            pos += 8;
        }
        assert(r.pos == pos);
    }
    /* Stale path: after a reset, a fresh reader of the old message reads nothing. */
    proto_header h2 = h;
    proto_reader r2;
    assert(proto_read(&a, &h2, &r2));
    assert(sc_arena_reset(&a));
    uint64_t v = 7;
    sc_str s = {NULL, 3, 0};
    assert(!proto_get_u64(&r2, &v) && v == 7);
    assert(!proto_get_str(&r2, &s) && s.len == 3);
    assert(!proto_read(&a, &h2, &r2) && h2.req == h.req);
}

static sim S;

/* The glue's libpq side: what a result stream may do next. */
typedef struct {
    bool held;            /* an event waits for credit */
    conn_worker_event_kind ev; /* which one */
} pq_side;

static void pq_event(pq_side *pq, conn_worker_event_kind k, uint64_t per_chunk) {
    if (k == W_EV_ROW) {
        if (sim_row(&S, per_chunk, 0) == ACT_WAIT) {
            pq->held = true;
            pq->ev = k;
        }
        return;
    }
    assert(sim_pq(&S, k, 0, 0));
    if (S.waiting) {
        pq->held = true;
        pq->ev = k;
    }
}

static void fuzz_script(const uint8_t *data, size_t size) {
    if (size < 4) {
        return;
    }
    conn_limits lim = {1u + data[0] % 4u, 1u + data[1] % 8u, 1u + data[2] % 40u};
    size_t arenas = 3u + data[3] % 20u;
    uint64_t per_chunk = 1u + data[0] / 64u * 3u;
    sim_init(&S, arenas, lim);
    S.cancel_async = (data[2] & 0x80u) != 0; /* libpq 17+: the script ends each cancel */
    pq_side pq = {false, W_EV_TICK};
    uint64_t now = 0;
    for (size_t i = 4; i < size; i++) {
        uint8_t b = data[i];
        now += b % 64u;
        unsigned pick = b % 16u;
        if (pick == 0) {
            assert(sim_ui(&S, CONN_UI_EV_OPEN));
        } else if (pick == 1) {
            assert(sim_ui(&S, CONN_UI_EV_RUN));
        } else if (pick == 2) {
            assert(sim_ui(&S, CONN_UI_EV_CANCEL));
        } else if (pick == 3 && b < 32) {
            assert(sim_ui(&S, CONN_UI_EV_CLOSE));
        } else if (pick <= 5) {
            if (sim_to_worker(&S) && pq.held) {
                /* Once the query is over (CLOSE, or LOST) libpq has no more
                 * results for it and the glue drops what it held. */
                pq.held = false;
                if (w_busy(&S.wk)) {
                    pq_event(&pq, pq.ev, per_chunk);
                }
            }
        } else if (pick <= 7) {
            (void)sim_to_ui(&S);
        } else if (!pq.held) {
            worker_state st = S.wk.state;
            unsigned c = b / 16u;
            if (S.wk.cancel_out && c >= 14) {
                assert(sim_pq(&S, c == 14 ? W_EV_CANCEL_DONE : W_EV_CANCEL_FAILED, 0, now));
            } else if (S.wk.query_held) {
                assert(sim_pq(&S, W_EV_TICK, 0, now)); /* no results before its query goes out */
            } else if (st == W_CONNECTING) {
                conn_worker_event_kind k = c < 10 ? W_EV_CONNECTED : c < 14 ? W_EV_CONN_FAILED : W_EV_LOST;
                if (k == W_EV_CONN_FAILED) {
                    assert(sim_pq(&S, k, c % 2u, now));
                } else {
                    assert(sim_pq(&S, k, 0, now));
                }
            } else if (st == W_RUNNING || st == W_CANCELLING || st == W_DRAINING) {
                conn_worker_event_kind k = W_EV_TICK;
                if (c < 6) {
                    k = S.wk.in_set ? W_EV_ROW : W_EV_COLUMNS;
                } else if (c < 8) {
                    k = W_EV_COLUMNS;
                } else if (c < 10) {
                    k = W_EV_TAG;
                } else if (c == 10) {
                    k = W_EV_ERROR;
                } else if (c == 11) {
                    k = W_EV_END;
                } else if (c == 12) {
                    k = W_EV_LOST;
                }
                if (k == W_EV_ROW && st == W_RUNNING && !S.wk.in_set) {
                    k = W_EV_COLUMNS;
                }
                if (k == W_EV_TICK) {
                    assert(sim_pq(&S, k, 0, now));
                } else {
                    pq_event(&pq, k, per_chunk);
                }
            } else if (st == W_READY) {
                assert(sim_pq(&S, c == 0 ? W_EV_LOST : W_EV_TICK, 0, now));
            } else {
                assert(sim_pq(&S, W_EV_TICK, 0, now));
            }
        }
        /* A live query on a live connection has its terminal arena out, unless the
         * worker already lost the connection and handed it back. */
        assert(!q_active(S.ui.q) || S.ui.state != UI_CONN_CONNECTED || S.ui.terminal_out ||
               S.wk.state >= W_CLOSING); /* LOST is still on its way */
        /* The set holds at most its quota; one more credit may be the spare. */
        assert(S.ui.held_count <= lim.quota);
        assert(S.ui.held_count + S.ui.outstanding <= lim.quota + 1 || S.ui.sets == 0);
        assert(S.wk.grant_count <= S.ui.outstanding || S.wk.req != S.ui.req);
        assert(S.ui.held_count == 0 || S.ui.sets > 0);
        assert(!S.exited || S.ui.state == UI_CONN_CLOSING || S.ui.state == UI_CONN_CLOSED);
    }
    /* Quit: CLOSE, then deliver everything, re-delivering a held event. */
    bool started = sim_count(S.ulog, 0, S.ulog_n, ACT_SEND, PROTO_OPEN) > 0;
    assert(sim_ui(&S, CONN_UI_EV_CLOSE));
    for (;;) {
        bool moved = sim_to_worker(&S);
        moved = sim_to_ui(&S) || moved;
        if (!moved) {
            break;
        }
    }
    assert(S.ui.state == UI_CONN_CLOSED);
    if (started) {
        assert(S.exited && S.wk.state == W_CLOSED);
    }
    size_t held = S.ui.held_count + (S.ui.terminal_kept ? 1u : 0u);
    assert(S.pool.free + held == S.pool.count);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) {
        return 0;
    }
    if (data[0] % 2 == 0) {
        fuzz_header(data + 1, size - 1, data[0] % 4 == 2);
    } else {
        fuzz_script(data + 1, size - 1);
    }
    return 0;
}
