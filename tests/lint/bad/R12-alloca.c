#include <stddef.h>

void *stack(size_t n) {
    return __builtin_alloca(n);
}
