#include <stddef.h>

typedef struct {
    int *ptr;
    size_t len;
} view;

int *skip(view s, size_t n) {
    return s.ptr + n;
}
