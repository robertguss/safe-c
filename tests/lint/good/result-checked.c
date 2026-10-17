#include <stdbool.h>
#include <stddef.h>

struct size_result {
    bool ok;
    size_t value;
    int err;
};

struct size_option {
    bool some;
    size_t value;
};

size_t guarded_then(struct size_result r) {
    if (r.ok) {
        return r.value;
    }
    return 0;
}

size_t guarded_else(struct size_result r) {
    if (!r.ok) {
        return 0;
    } else {
        return r.value;
    }
}

size_t early_out(struct size_result r) {
    if (!r.ok) {
        return 0;
    }
    return r.value;
}

size_t ternary(struct size_result r) {
    return r.ok ? r.value : 0;
}

size_t short_circuit(const struct size_option *o) {
    if (o->some && o->value > 4) {
        return o->value;
    }
    return 0;
}

size_t through_pointer(const struct size_option *o) {
    if (!o->some) {
        return 0;
    }
    return o->value;
}
