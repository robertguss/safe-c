#include "safec/result.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>

int main(void) {
    const size_t edges[] = {0, 1, 7, SIZE_MAX};
    for (size_t i = 0; i < sizeof edges / sizeof edges[0]; i++) {
        size_t v = edges[i];

        sc_option_size some = sc_some_size(v);
        assert(some.some && some.value == v);
        assert(sc_option_size_is_some(some));
        assert(sc_option_size_unwrap(some) == v);
        assert(sc_option_size_or(some, 99) == v);

        sc_result_size ok = sc_ok_size(v);
        assert(ok.ok && ok.value == v && ok.err == 0);
        assert(sc_result_size_is_ok(ok));
        assert(sc_result_size_unwrap(ok) == v);
        assert(sc_result_size_or(ok, 99) == v);
    }

    sc_option_size none = sc_none_size();
    assert(!none.some && none.value == 0);
    assert(!sc_option_size_is_some(none));
    assert(sc_option_size_or(none, 0) == 0);
    assert(sc_option_size_or(none, SIZE_MAX) == SIZE_MAX);

    const int codes[] = {INT_MIN, -1, 0, 1, INT_MAX};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        sc_result_size err = sc_err_size(codes[i]);
        assert(!err.ok && err.value == 0 && err.err == codes[i]);
        assert(!sc_result_size_is_ok(err));
        assert(sc_result_size_unwrap_err(err) == codes[i]);
        assert(sc_result_size_or(err, 5) == 5);
        assert(sc_result_size_or(err, SIZE_MAX) == SIZE_MAX);
    }

    /* A zero value is still a present value: the tag decides, not the payload. */
    assert(sc_option_size_is_some(sc_some_size(0)));
    assert(sc_result_size_is_ok(sc_ok_size(0)));
    assert(sc_result_size_or(sc_ok_size(0), 9) == 0);
    assert(sc_option_size_or(sc_some_size(0), 9) == 0);
    return 0;
}
