#include <stdint.h>

uint8_t *second_half(void *block, unsigned long half) {
    return (uint8_t *)block + half;
}
