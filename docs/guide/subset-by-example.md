# The subset by example

Application C stays inside [subset v1](../subset-v1.md): rules R0 to R14. Lint
enforces them, and each section here shows code that breaks one rule, the
`lint-rule` id it earns, and the form that passes. Every example below is
compiled and linted by `make guide-test`: a `bad` example must raise the rule
named, and a `good` example must pass with no findings.

Most rules say the same thing in different words: memory comes from an arena,
reads go through a checked accessor, and a failure is a value the caller must
look at.

## R1 and R2: no subscripts, no pointer arithmetic

Indexing and pointer arithmetic are how C reads past the end of a buffer.

```c bad R1-no-subscript
int first(int *items, int count) {
    if (count <= 0) {
        return 0;
    }
    return items[0];
}
```

```c bad R2-no-ptr-arith
int second(int *items, int count) {
    if (count < 2) {
        return 0;
    }
    return *(items + 1);
}
```

A slice carries its own length and its owner's epoch, and `sc_slice_get`
refuses anything out of range or stale. It writes the byte through an
out-parameter and returns whether it did.

```c good
#include <safec/slice.h>

/* Byte i of s, or 0 when s is stale or i is out of range. */
uint8_t byte_or_zero(const sc_arena *a, sc_slice s, size_t i) {
    uint8_t b = 0;
    if (!sc_slice_get(a, s, i, &b)) {
        return 0;
    }
    return b;
}

/* Everything after the first byte; empty when there is nothing after it. */
sc_slice tail(const sc_arena *a, sc_slice s) {
    if (s.len == 0) {
        return s;
    }
    return sc_slice_sub(a, s, 1, s.len - 1);
}
```

A loop over a slice is an index loop with a `sc_slice_get` in the body. The
index is an integer, so `i++` is fine; only pointer arithmetic is banned.

```c good
#include <safec/slice.h>

/* The sum of the bytes of s, or 0 if s is stale. */
size_t total(const sc_arena *a, sc_slice s) {
    size_t sum = 0;
    for (size_t i = 0; i < s.len; i++) {
        uint8_t b = 0;
        if (!sc_slice_get(a, s, i, &b)) {
            return 0;
        }
        sum += b;
    }
    return sum;
}
```

## R3: no `char *`

A `char *` carries no length, and the C library will read until it finds a
zero. Strings are `sc_str`, a byte slice with an explicit length.

```c bad R3-no-char-ptr
int starts(const char *text) {
    if (text == 0) {
        return 0;
    }
    return 1;
}
```

`unsigned char *` and `signed char *` are byte pointers, not text, so R3 lets
them through, as it does `uint8_t *`. A cast to `char *` is still R3, and
arithmetic on a cast, `(uint8_t *)block + n`, is R2: to lay several objects
out in one allocation, take one `sc_slice` from the arena and cut it with
`sc_slice_sub`.

`SC_STR_LIT` copies a literal into an arena, which is how a constant becomes a
string. To compare a string with a constant, do not copy the constant at all:
`SC_STR_EQ_LIT` reads the literal in place.

```c good
#include <safec/str.h>

/* Whether the method string is exactly GET. */
bool is_get(const sc_arena *a, sc_str method) {
    return SC_STR_EQ_LIT(a, method, "GET");
}
```

The older form still compiles and lints clean, but it spends arena space on
every call, rounded up to eight bytes. In a request handler that is a leak
until the arena resets.

```c avoid
#include <safec/str.h>

/* Copies "GET" into the arena on every call. Prefer SC_STR_EQ_LIT. */
bool is_get_copying(sc_arena *a, sc_str method) {
    sc_str get = SC_STR_LIT(a, "GET");
    return sc_str_eq(a, method, get);
}
```

## R5, R6, R7 and R12: no heap, no arrays, no stack allocation

These four close every other way to get memory. `malloc` and friends, a
declared array, a variable length array and `alloca` are all banned in
application code.

```c bad R5-no-malloc-free
#include <stdlib.h>

void *grab(size_t n) {
    return malloc(n);
}
```

```c bad R6-no-raw-array
int raw_array(void) {
    int a[4] = {0};
    return (int)sizeof a;
}
```

```c bad R7-no-vla
int bump(int n) {
    int buf[n];
    return (int)sizeof buf;
}
```

```c bad R12-no-alloca
#include <stddef.h>

void *stack(size_t n) {
    return __builtin_alloca(n);
}
```

Take bytes from an arena instead. `sc_arena_alloc` rounds the size up to eight
bytes and returns the canonical empty slice (length 0) when the arena is full
or `n` is 0, so test the length before you use the slice. Everything allocated
from an arena goes away together when you call `sc_arena_reset`; a slice taken
before a reset is stale afterwards and every operation on it refuses.

```c good
#include <safec/arena.h>

/* n fresh bytes from the arena into *out; false if the arena is full or n is 0. */
bool take(sc_arena *a, size_t n, sc_slice *out) {
    sc_slice s = sc_arena_alloc(a, n);
    if (n == 0 || s.len != n) {
        return false;
    }
    *out = s;
    return true;
}
```

The arena itself needs backing bytes once. That one array lives in an adapter
file (R0, below).

## R4: no varargs

A variadic function trusts the caller to pass what the format says. Pass a
slice, or call a fixed-arity function more than once.

```c bad R4-no-varargs
#include <stdarg.h>

int sum_two(int count, ...) {
    va_list args;
    va_start(args, count);
    int first = va_arg(args, int);
    va_end(args);
    return first;
}
```

The `total` function under R1 is the replacement: the count travels with the
data.

## R8: no `goto`

```c bad R8-no-goto
int abs_of(int n) {
    if (n >= 0) {
        return n;
    }
    goto flip;
flip:
    return -n;
}
```

Write the early return, or the branch, directly.

```c good
/* The absolute value of n; n must not be INT_MIN. */
int abs_of(int n) {
    if (n >= 0) {
        return n;
    }
    return -n;
}
```

## R9: no `errno`

`errno` is a global that any call may overwrite. A failure that matters is a
value in the return type: `sc_result_size` holds a `size_t` and an `int` error
code, and `sc_option_size` holds a `size_t` or nothing.

```c bad R9-no-errno
#include <errno.h>

int err(void) {
    errno = 0;
    return errno;
}
```

```c good
#include <safec/result.h>

/* Half of n, or error code 1 when n is odd. */
sc_result_size half(size_t n) {
    if (n % 2 != 0) {
        return sc_err_size(1);
    }
    return sc_ok_size(n / 2);
}
```

## R13: read a result's value only after its tag

A result or option is a struct with a tag (`ok` or `some`) and a payload
(`value`). Reading `value` of a failed result gives zero, which looks like a
real answer. Lint rejects the read unless the same function checks the tag
first.

```c bad R13-unchecked-result
#include <safec/result.h>

size_t read_without_check(sc_result_size r) {
    return r.value;
}
```

Three forms pass. Test the tag and return early, test it in the condition, or
use an accessor.

```c good
#include <safec/result.h>

size_t early_out(sc_result_size r) {
    if (!r.ok) {
        return 0;
    }
    return r.value;
}

size_t in_condition(sc_option_size o) {
    return o.some ? o.value : 0;
}

size_t with_accessor(sc_result_size r) {
    return sc_result_size_or(r, 0);
}
```

The early-out must be one `return` that does not read `value`. Write the
positive check (`r.ok`, not `r.ok == false`). `sc_result_size_unwrap` has a
contract that requires `r.ok`, so calling it without the check is a proof
failure, not only a lint failure; see [Contracts](contracts.md).

## R10: no recursion

Recursion has no bound the checker can see, and it can exhaust the stack on
hostile input.

```c bad R10-no-recursion
int down(int n) {
    if (n <= 0) {
        return 0;
    }
    return down(n - 1);
}
```

Use a loop whose bound is visible in its header.

```c good
/* The number of steps from n down to zero. */
int down(int n) {
    int steps = 0;
    for (int i = n; i > 0; i--) {
        steps++;
    }
    return steps;
}
```

## R11 and tidy-narrowing: no implicit conversion

Mixing signed and unsigned, or narrowing a wide type into a narrow one, changes
values silently. The compiler warns (`-Wconversion -Wsign-conversion`) and lint
turns the warning into a failure.

```c bad R11-no-implicit-conversion
void take_unsigned(unsigned value);

void pass_signed(int n) {
    take_unsigned(n);
}
```

```c bad tidy-narrowing
void narrow(int value) {
    short small = value;
    (void)small;
}
```

Check the range, then cast on purpose.

```c good
void take_unsigned(unsigned value);

/* Passes n on when it is not negative. */
void pass_signed(int n) {
    if (n < 0) {
        return;
    }
    take_unsigned((unsigned)n);
}
```

## R14: no suppressions

A pragma can switch off the very warnings R7 and R11 rely on, and a `NOLINT`
comment hides a clang-tidy finding. Lint rejects every `#pragma`, `_Pragma`
and `NOLINT` in application C, and runs Semgrep with `--disable-nosem`, so a
`nosemgrep` comment hides nothing. Use include guards rather than
`#pragma once`.

```c bad R14-no-suppression
#pragma clang diagnostic ignored "-Wconversion"
short narrow(int value) {
    return value;
}
```

The fix is the fix for the finding the pragma was hiding: here, check the
range and cast, as in R11 above.

## R0: adapters

Some code has to be outside the subset: it calls a C library that takes a
`char *`, or it owns a raw array. That code goes in a file under `adapters/`,
which lint skips, and gets one row in the R0 table of
[`subset-v1.md`](../subset-v1.md). The adapter hands the application only
subset-friendly types.

```c adapter
#include <safec/arena.h>

static _Alignas(8) uint8_t backing[512];

/* Initializes *a over static backing storage. */
void adopter_arena_init(sc_arena *a) {
    sc_arena_init(a, backing, sizeof backing);
}
```

Keep adapters small. The HTTP demo's single adapter
([`adapters/microhttpd.c`](../../adapters/microhttpd.c)) owns every call into
libmicrohttpd and every `malloc`, and gives the handler only an `sc_str` and an
`sc_arena`.
