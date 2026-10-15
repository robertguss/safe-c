#include <stdlib.h>

#define GET(n) malloc(n)

void *grab(size_t n) {
    return GET(n);
}
