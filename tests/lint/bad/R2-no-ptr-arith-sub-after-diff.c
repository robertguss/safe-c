unsigned long span(const int *start, const int *end, unsigned long n) {
    const int *back = end - n;
    return (unsigned long)(back - start);
}
