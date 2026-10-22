static unsigned long span(const int *start, const int *end) {
    return (unsigned long)(end - start);
}

int main(void) {
    int x = 0;
    return span(&x, &x) == 0 ? 0 : 1;
}
