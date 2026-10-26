/* Fuzzes the thread adapter's wake and flag on one thread against a model: a
 * wake is pending after any ring until a drain, and a flag is set after a set
 * until a clear. Input bytes pick the operations and how many times to ring. */
#include "../adapters/thread.c"
#include <assert.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    sc_wake w;
    assert(sc_wake_new(&w).ok);
    sc_flag f = {0};
    bool pending = false, set = false;
    for (size_t k = 0; k < size; k++) {
        uint8_t op = data[k];
        switch (op % 5) {
        case 0: {
            /* 1 to 32 rings; tests/thread_test.c overfills the pipe, which is too slow here. */
            size_t rings = (size_t)(op >> 3) + 1;
            for (size_t i = 0; i < rings; i++)
                assert(sc_wake_ring(w).ok);
            pending = true;
            break;
        }
        case 1: {
            sc_result_size r = sc_wake_drain(w);
            assert(r.ok && r.value == (pending ? 1u : 0u));
            pending = false;
            break;
        }
        case 2: {
            sc_result_size r = sc_wake_wait(w, 0);
            assert(r.ok && r.value == (pending ? 1u : 0u));
            break;
        }
        case 3:
            assert(sc_flag_set(&f) == !set);
            set = true;
            break;
        default:
            sc_flag_clear(&f);
            set = false;
            break;
        }
        assert(sc_flag_is_set(&f) == set);
    }
    sc_wake_free(&w);
    assert(!sc_wake_ring(w).ok && !sc_wake_wait(w, 0).ok && !sc_wake_drain(w).ok);
    return 0;
}
