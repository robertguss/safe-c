#include "safec/result.h"

sc_option_size sc_some_size(size_t v) {
    sc_option_size o = {true, v};
    return o;
}

sc_option_size sc_none_size(void) {
    sc_option_size o = {false, 0};
    return o;
}

bool sc_option_size_is_some(sc_option_size o) {
    return o.some;
}

size_t sc_option_size_unwrap(sc_option_size o) {
    return o.value;
}

size_t sc_option_size_or(sc_option_size o, size_t fallback) {
    if (o.some)
        return o.value;
    return fallback;
}

sc_result_size sc_ok_size(size_t v) {
    sc_result_size r = {true, v, 0};
    return r;
}

sc_result_size sc_err_size(int e) {
    sc_result_size r = {false, 0, e};
    return r;
}

bool sc_result_size_is_ok(sc_result_size r) {
    return r.ok;
}

size_t sc_result_size_unwrap(sc_result_size r) {
    return r.value;
}

int sc_result_size_unwrap_err(sc_result_size r) {
    return r.err;
}

size_t sc_result_size_or(sc_result_size r, size_t fallback) {
    if (r.ok)
        return r.value;
    return fallback;
}
