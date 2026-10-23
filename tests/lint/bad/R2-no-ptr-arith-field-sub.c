#include <stddef.h>

typedef struct {
    int *ptr;
    size_t len;
} view;

int *back(view s, size_t n) {
    return s.ptr - n;
}
