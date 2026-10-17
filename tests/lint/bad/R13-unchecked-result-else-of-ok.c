#include <stdbool.h>
#include <stddef.h>

struct size_result {
    bool ok;
    size_t value;
    int err;
};

size_t use(size_t v);

size_t read_in_else_of_ok(struct size_result r) {
    if (r.ok) {
        return 1;
    } else {
        return use(r.value);
    }
}
