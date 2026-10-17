#include <stdbool.h>
#include <stddef.h>

struct size_option {
    bool some;
    size_t value;
};

size_t read_through_pointer(const struct size_option *o) {
    return o->value;
}
