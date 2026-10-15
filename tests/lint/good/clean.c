static int answer(void) {
    int value = 4;
    return value;
}

int main(void) {
    int value = answer();
    if (value == 4) {
        return 0;
    }
    return 1;
}
