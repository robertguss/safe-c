#include "safec/arena.h"
#include "safec/handle.h"
#include <assert.h>
#include <stdalign.h>
#include <string.h>

#define CAP_MAX 8
#define KEPT 16

/* Reference model: a plain array of states and generations with the same
 * rules, kept beside the table and compared after every operation. */
typedef struct {
    unsigned state[CAP_MAX];
    unsigned gen[CAP_MAX];
    size_t cap;
} model;

static bool ref_live(const model *m, sc_handle h) {
    return h.index < m->cap && m->state[h.index] == 1 && m->gen[h.index] == h.generation;
}

static void check_bytes(const model *m, const uint8_t *slots) {
    for (size_t i = 0; i < m->cap; i++) {
        assert(slots[3 * i] == m->state[i]);
        assert(slots[3 * i + 1] == (m->gen[i] & 0xFF));
        assert(slots[3 * i + 2] == (m->gen[i] >> 8));
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2)
        return 0;
    alignas(8) uint8_t storage[32];
    sc_arena a;
    sc_arena_init(&a, storage, sizeof storage);
    model m = {{0}, {0}, data[0] % (CAP_MAX + 1)};
    sc_handle_table t = sc_handle_table_new(&a, m.cap);
    if (m.cap == 0) {
        assert(t.cap == 0 && t.slots.len == 0 && a.used == 0);
        return 0;
    }
    assert(t.cap == m.cap && t.slots.ptr == storage);

    /* Seed each slot's generation from the input so the fuzzer reaches the
     * carry at 255 and retirement at SC_HANDLE_GEN_MAX in a few steps. */
    uint8_t seed = data[1];
    for (size_t i = 0; i < m.cap; i++) {
        unsigned g = (seed & 1) ? SC_HANDLE_GEN_MAX - (unsigned)(i % 3) : (seed & 2) ? 255u : 0u;
        m.gen[i] = g;
        storage[3 * i + 1] = (uint8_t)(g & 0xFF);
        storage[3 * i + 2] = (uint8_t)(g >> 8);
    }
    check_bytes(&m, storage);

    sc_handle kept[KEPT];
    size_t nkept = 0;
    for (size_t p = 2; p + 1 < size; p += 2) {
        uint8_t op = data[p] % 4;
        uint8_t arg = data[p + 1];
        sc_handle h = nkept > 0 ? kept[arg % nkept] : (sc_handle){arg % (CAP_MAX + 1), 0};
        if ((arg & 0xC0) == 0xC0) /* a forged handle, near a real one */
            h.generation++;
        if ((arg & 0xA0) == 0xA0)
            h.index += m.cap;
        if (op == 0) {
            size_t want = 0;
            while (want < m.cap && m.state[want] != 0)
                want++;
            sc_handle out = {1234, 1234};
            bool ok = sc_handle_alloc(&a, t, &out);
            assert(ok == (want < m.cap));
            if (ok) {
                assert(out.index == want && out.generation == m.gen[want]);
                m.state[want] = 1;
                kept[nkept < KEPT ? nkept++ : arg % KEPT] = out;
            } else {
                assert(out.index == 1234 && out.generation == 1234);
            }
        } else if (op == 1) {
            bool live = ref_live(&m, h);
            assert(sc_handle_free(&a, t, h) == live);
            if (live) {
                if (m.gen[h.index] == SC_HANDLE_GEN_MAX) {
                    m.state[h.index] = 2;
                } else {
                    m.state[h.index] = 0;
                    m.gen[h.index]++;
                }
            }
        } else {
            sc_option_size o = sc_handle_get(&a, t, h);
            assert(o.some == ref_live(&m, h));
            assert(o.value == (o.some ? h.index : 0));
        }
        check_bytes(&m, storage);
    }

    /* Stale path: after the owner resets, every old handle is rejected and
     * nothing writes to the storage. */
    assert(sc_arena_reset(&a));
    uint8_t before[sizeof storage];
    memcpy(before, storage, sizeof storage);
    for (size_t k = 0; k < nkept; k++) {
        assert(!sc_handle_get(&a, t, kept[k]).some);
        assert(!sc_handle_free(&a, t, kept[k]));
    }
    sc_handle out = {7, 7};
    assert(!sc_handle_alloc(&a, t, &out) && out.index == 7 && out.generation == 7);
    assert(memcmp(before, storage, sizeof storage) == 0);
    return 0;
}
