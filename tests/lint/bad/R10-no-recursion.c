int down(int n) {
    if (n <= 0) {
        return 0;
    }
    return down(n - 1);
}
