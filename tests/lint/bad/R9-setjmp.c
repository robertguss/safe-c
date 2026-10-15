#include <setjmp.h>

static jmp_buf mark;

int hop(void) {
    int ready = setjmp(mark);
    if (ready != 0) {
        return ready;
    }
    longjmp(mark, 1);
}
