#include <alloca.h>
#include <stddef.h>

int scratch(size_t n) {
    void *buf = alloca(n);
    if (buf == 0) {
        return 0;
    }
    return 1;
}
