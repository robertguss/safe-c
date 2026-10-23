#include <stddef.h>

typedef struct {
    int *ptr;
    size_t len;
} view;

/* The SC-13 shape: a pointer variable named `len` is in scope while `s.len`
   is an integer field. Arithmetic on the integer field is not R2. */
static size_t grow(view s, size_t n, size_t *len) {
    *len = n;
    return s.len + n;
}

static size_t shrink(const view *p, size_t n) {
    return p->len - n;
}

int main(void) {
    size_t out = 0;
    view v = {0, 4};
    return grow(v, 1, &out) == 5 && shrink(&v, 1) == 3 ? 0 : 1;
}
