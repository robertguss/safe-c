typedef struct {
    int *ptr;
    unsigned long len;
} view;

static unsigned long remaining(view v, unsigned long pos, unsigned long *len) {
    *len = pos;
    return v.len - pos;
}

int main(void) {
    unsigned long n = 0;
    view v = {0, 4};
    return remaining(v, 1, &n) == 3 ? 0 : 1;
}
