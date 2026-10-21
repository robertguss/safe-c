#include <stdint.h>

#define QUIET _Pragma("clang diagnostic ignored \"-Wconversion\"")
QUIET
uint8_t low_byte(int n) {
    return n;
}
