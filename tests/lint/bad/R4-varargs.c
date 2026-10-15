#include <stdarg.h>

int sum_two(int count, ...) {
    va_list args;
    va_start(args, count);
    int first = va_arg(args, int);
    va_end(args);
    return first;
}
