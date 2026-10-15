#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
int raw_array(void) { int a[4] = {0}; return a[1]; }
int ptr_arith(int *p) { return *(p + 1); }
int cstr(const char *s) { return s[0]; }
void *heap(void) { void *p = malloc(8); free(p); return 0; }
void *stack(int n) { return __builtin_alloca(n); }
int vla(int n) { int v[n]; v[0] = 1; return v[0]; }
int va(int n, ...) { va_list ap; va_start(ap, n); int x = va_arg(ap, int); va_end(ap); return x; }
int jmp(int x) { if (x) goto out; x = 2; out: return x; }
int rec(int n) { return n ? rec(n - 1) : 0; }
unsigned conv(int x) { unsigned u = x; short s = x; return u + (unsigned)s; }
int err(void) { errno = 0; return errno; }
