#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *ptr;
    size_t len;
} view;

uint8_t *skip(view s, size_t n) {
    return (uint8_t *)s.ptr + n;
}
