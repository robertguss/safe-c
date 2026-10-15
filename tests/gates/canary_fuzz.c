#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 0U && *data == 0x41U) {
        int values[4] = {1, 2, 3, 4};
        int *ptr = values;
        volatile int index = 4;
        return ptr[index];
    }
    return 0;
}
