#include <stdbool.h>
#include <stddef.h>

struct size_result {
    bool ok;
    size_t value;
    int err;
};

size_t read_on_error_path(struct size_result r) {
    if (!r.ok) {
        return r.value;
    }
    return 0;
}
