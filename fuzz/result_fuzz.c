#include "safec/result.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

/* The reference is the expected tag and payload, computed from the input
 * without calling the module; the constructors and accessors are checked
 * against it for every input. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2 * sizeof(size_t) + sizeof(int) + 1)
        return 0;
    size_t v, fallback;
    int e;
    memcpy(&v, data, sizeof v);
    memcpy(&fallback, data + sizeof v, sizeof fallback);
    memcpy(&e, data + 2 * sizeof v, sizeof e);
    bool tag = data[2 * sizeof v + sizeof e] % 2 == 0;

    sc_option_size o = tag ? sc_some_size(v) : sc_none_size();
    assert(o.some == tag);
    assert(o.value == (tag ? v : 0));
    assert(sc_option_size_is_some(o) == tag);
    assert(sc_option_size_or(o, fallback) == (tag ? v : fallback));
    if (tag)
        assert(sc_option_size_unwrap(o) == v);

    sc_result_size r = tag ? sc_ok_size(v) : sc_err_size(e);
    assert(r.ok == tag);
    assert(r.value == (tag ? v : 0));
    assert(r.err == (tag ? 0 : e));
    assert(sc_result_size_is_ok(r) == tag);
    assert(sc_result_size_or(r, fallback) == (tag ? v : fallback));
    if (tag)
        assert(sc_result_size_unwrap(r) == v);
    else
        assert(sc_result_size_unwrap_err(r) == e);
    return 0;
}
