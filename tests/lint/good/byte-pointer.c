#include <stdint.h>

/* A byte pointer is not text: `unsigned char *` reads like `uint8_t *`. */
static int is_zero(const unsigned char *bytes, const signed char *signed_bytes) {
    return bytes == 0 && signed_bytes == 0;
}

int main(void) {
    return is_zero(0, 0) ? 0 : 1;
}
