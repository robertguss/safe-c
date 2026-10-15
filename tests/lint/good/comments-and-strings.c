/* char *p; malloc(8); alloca(8); */
int main(void) {
    /* -Wconversion -Wvla */
    return (int)sizeof "char * malloc(";
}
