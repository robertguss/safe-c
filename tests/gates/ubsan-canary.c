#include <limits.h>

int main(void) {
    volatile int max = INT_MAX;
    volatile int step = 1;
    int overflow = max + step;
    return overflow == 0 ? 0 : 1;
}
