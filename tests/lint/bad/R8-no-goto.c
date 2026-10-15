int abs_of(int n) {
    if (n >= 0) {
        return n;
    }
    goto flip;
flip:
    return -n;
}
