#include <stddef.h>
#include <alloca.h>

#define STK(n) alloca(n)

void *stack_macro(size_t n) {
    return STK(n);
}
