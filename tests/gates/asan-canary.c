int main(void) {
    int values[4] = {1, 2, 3, 4};
    int *ptr = values;
    volatile int index = 4;
    return ptr[index];
}
