#include <errno.h>

int err(void) {
    errno = 0;
    return errno;
}
