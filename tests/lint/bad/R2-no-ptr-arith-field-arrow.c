#include <stddef.h>

typedef struct {
    int *ptr;
    size_t len;
} view;

int *skip(const view *p, size_t n) {
    return p->ptr + n;
}
