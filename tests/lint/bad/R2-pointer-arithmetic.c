int second(int *items, int count) {
    if (count < 2) {
        return 0;
    }
    return *(items + 1);
}
