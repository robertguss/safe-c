#include <stdbool.h>
#include <stddef.h>

struct size_result {
    bool ok;
    size_t value;
    int err;
};

size_t use(size_t v);

size_t read_after_nested_guard(struct size_result r) {
    if (!r.ok) {
        if (r.err != 0) {
            return 0;
        }
    }
    return r.value;
}
