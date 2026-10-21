// NOLINTBEGIN(misc-no-recursion)
int fact(int n) {
    return n <= 1 ? 1 : n * fact(n - 1);
}
// NOLINTEND(misc-no-recursion)
