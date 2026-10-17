#include <stdbool.h>
#include <stddef.h>

struct size_result {
    bool ok;
    size_t value;
    int err;
};

size_t read_without_check(struct size_result r) {
    return r.value;
}
