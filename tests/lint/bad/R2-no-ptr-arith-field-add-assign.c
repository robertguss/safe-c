#include <stddef.h>

typedef struct {
    int *ptr;
    size_t len;
} view;

void advance(view *s, size_t n) {
    s->ptr += n;
}
