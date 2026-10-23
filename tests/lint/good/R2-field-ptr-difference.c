#include <stddef.h>

typedef struct {
    int *ptr;
    size_t len;
} view;

static ptrdiff_t gap(view a, view b) {
    return a.ptr - b.ptr;
}

int main(void) {
    int x = 0;
    view v = {&x, 1};
    return gap(v, v) == 0 ? 0 : 1;
}
