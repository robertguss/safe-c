#include <stddef.h>

struct chunk {
    size_t used;
    unsigned char data[];
};

size_t flexible_member(const struct chunk *c) {
    return c->used;
}
