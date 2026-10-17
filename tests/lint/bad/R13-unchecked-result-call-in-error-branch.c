#include <stdbool.h>
#include <stddef.h>

struct size_result {
    bool ok;
    size_t value;
    int err;
};

size_t use(size_t v);

size_t read_inside_error_branch(struct size_result r) {
    if (!r.ok) {
        return use(r.value);
    }
    return 0;
}
