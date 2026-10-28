# Agent prompt for Safe C projects

Paste everything below the line into your project's `AGENTS.md`, `CLAUDE.md` or
agent system prompt. It assumes the project has installed Safe C as in the
[quickstart](guide/quickstart.md), so that `safec-lint` is on `PATH` and
`<safec/...>` headers resolve. Change the build command in "Done" to your own.
Every example is compiled and linted by `make guide-test`, and each declaration
is quoted from the headers, so the prompt cannot drift from the library.

Why each rule exists is in the [guide](guide/README.md). The prompt states the
rule and the replacement, which is what an agent needs mid-task.

---

# Writing C in the Safe C subset

You write C that stays inside the Safe C subset (v1). The subset removes the
constructs that cause memory errors, and the library gives you checked
replacements. Write for the checker: short functions, every failure handled,
every size checked before it is used.

## Done

A change is done when `safec-lint` passes with no findings and the project's
build and tests pass. Do not report work as finished while either is red.
Run `safec-lint` after every file you change, and fix the first finding before
the next, because one finding often causes others.

Never get green by weakening the check: no suppression comments, no excluding a
file from the lint run, no editing the rule pack, no moving code into an
adapter to dodge a rule. If the subset seems to block something the program
needs, stop and say what you need and why.

## What is banned and what to write instead

| Rule | Banned | Write instead |
| --- | --- | --- |
| R1, R2 | Subscripts `a[i]`, pointer arithmetic | `sc_slice_get`, `sc_slice_sub`; `SC_TSLICE_GET`/`SET` for typed arrays; an integer index loop |
| R3 | `char *` | `sc_str` (a byte slice) |
| R4 | Varargs | A slice, or a fixed-arity function |
| R5, R6, R7, R12 | `malloc`/`free`, declared arrays, VLAs, `alloca` | `sc_arena_alloc` for bytes; `sc_tslice` / `SC_TSLICE_*` for typed arrays |
| R8 | `goto` | Early `return`, or a branch |
| R9 | `errno` | `sc_result_size`, `sc_option_size` |
| R10 | Recursion | A loop with a visible bound |
| R11 | Implicit conversion, narrowing | Check the range, then cast explicitly |
| R13 | Reading `.value` without its tag | `if (!r.ok) { return ...; }` first, or `sc_result_size_or` |
| R14 | `#pragma`, `_Pragma`, `NOLINT` | Fix the finding; use include guards |
| R0 | Anything else outside the subset | An adapter file under `adapters/`, which a person approves |

## The library

Memory is an arena: a bump allocator over a buffer a small adapter supplies.
Everything you allocate from it is released together by a reset.

```c quote include/safec/arena.h
void sc_arena_init(sc_arena *a, uint8_t *buf, size_t cap);
/* ... */
sc_slice sc_arena_alloc(sc_arena *a, size_t n);
/* ... */
bool sc_arena_reset(sc_arena *a);
```

`sc_arena_alloc` returns an empty slice (length 0) when `n` is 0 or the arena
is full. Check `.len` before you use the slice. After `sc_arena_reset`, every
slice taken before it is stale and every operation on it refuses; never keep
one across a reset.

Reads and copies go through checked accessors that take the owning arena. A
stale or out-of-range call returns `false` and leaves the output untouched.

```c quote include/safec/slice.h
bool sc_slice_get(const sc_arena *a, sc_slice s, size_t i, uint8_t *out);
/* ... */
sc_slice sc_slice_sub(const sc_arena *a, sc_slice s, size_t start, size_t n);
/* ... */
bool sc_slice_copy(const sc_arena *a, sc_slice dst, sc_slice src);
/* ... */
bool sc_slice_move(const sc_arena *a, sc_slice dst, sc_slice src);
```

A typed array of integers or structs is an `sc_tslice`. Allocate with
`SC_TSLICE_ALLOC`; copy one element in or out with `SC_TSLICE_GET` and
`SC_TSLICE_SET`. Check `sc_tslice_len` before you use the result.

```c quote include/safec/tslice.h
sc_tslice sc_tslice_alloc(sc_arena *a, size_t count, size_t elem);
/* ... */
size_t sc_tslice_len(sc_tslice s);
/* ... */
#define SC_TSLICE_ALLOC(T, a, count) \
    ((void)sizeof(char[_Alignof(T) <= 8 ? 1 : -1]), sc_tslice_alloc((a), (count), sizeof(T)))
#define SC_TSLICE_GET(T, a, s, i, out) \
    sc_tslice_get((a), (s), (i), sizeof(T), (uint8_t *)(1 ? (out) : (T *)0))
#define SC_TSLICE_SET(T, a, s, i, in) \
    sc_tslice_set((a), (s), (i), sizeof(T), (const uint8_t *)(1 ? (in) : (const T *)0))
```

Strings are slices. Compare with a literal in place, and copy a literal into the
arena only when you need to keep it.

```c quote include/safec/str.h
#define SC_STR_LIT(a, lit) sc_str_from_lit((a), (const uint8_t *)(lit), sizeof(lit) - 1)
/* ... */
bool sc_str_eq(const sc_arena *a, sc_str x, sc_str y);
/* ... */
#define SC_STR_EQ_LIT(a, x, lit) sc_str_eq_lit((a), (x), (const uint8_t *)(lit), sizeof(lit) - 1)
/* ... */
bool sc_str_find(const sc_arena *a, sc_str hay, sc_str needle, size_t *out);
```

A value that may be missing is an option; one that may fail is a result.

```c quote include/safec/result.h
sc_option_size sc_some_size(size_t v);
/* ... */
sc_option_size sc_none_size(void);
/* ... */
sc_result_size sc_ok_size(size_t v);
/* ... */
sc_result_size sc_err_size(int e);
```

Handles name slots in a fixed table, and a handle goes stale when its slot is
freed or the arena resets:

```c quote include/safec/handle.h
sc_handle_table sc_handle_table_new(sc_arena *a, size_t cap);
/* ... */
bool sc_handle_alloc(const sc_arena *a, sc_handle_table t, sc_handle *out);
/* ... */
bool sc_handle_free(const sc_arena *a, sc_handle_table t, sc_handle h);
/* ... */
sc_option_size sc_handle_get(const sc_arena *a, sc_handle_table t, sc_handle h);
```

A byte builder appends into a destination slice. An append that does not fit
writes nothing and sets a sticky overflow flag, so a later append also fails
and a label that did not fit is detected rather than cut in the middle of a
number.

```c quote include/safec/buf.h
sc_buf sc_buf_new(sc_slice dst);
/* ... */
bool sc_buf_u64(const sc_arena *a, sc_buf *b, uint64_t v);
```

UTF-8 over byte strings, as RFC 3629 defines it. An invalid sequence, or one
cut short, has a skip length of 1. A stale string is treated as empty where
the function says so.

```c quote include/safec/utf8.h
sc_utf8_char sc_utf8_next(const sc_arena *a, sc_str s, size_t i);
/* ... */
bool sc_utf8_valid(const sc_arena *a, sc_str s);
```

## Patterns

Scan a slice with an integer index and a checked read. Handle the failed read
in the loop; do not assume the slice is live.

```c good
#include <safec/slice.h>

/* The offset of the first byte equal to c, found in s, or len when absent. */
size_t find_byte(const sc_arena *a, sc_slice s, uint8_t c) {
    for (size_t i = 0; i < s.len; i++) {
        uint8_t b = 0;
        if (!sc_slice_get(a, s, i, &b)) {
            return s.len;
        }
        if (b == c) {
            return i;
        }
    }
    return s.len;
}
```

Return a result when the caller must tell failure apart, and check the tag
before you read the value.

```c good
#include <safec/result.h>
#include <safec/slice.h>

/* Index of the first zero byte of s, or error code 1 when there is none. */
sc_result_size first_zero(const sc_arena *a, sc_slice s) {
    for (size_t i = 0; i < s.len; i++) {
        uint8_t b = 1;
        if (!sc_slice_get(a, s, i, &b)) {
            return sc_err_size(1);
        }
        if (b == 0) {
            return sc_ok_size(i);
        }
    }
    return sc_err_size(1);
}

/* The offset of the first zero byte, or fallback. */
size_t zero_or(const sc_arena *a, sc_slice s, size_t fallback) {
    sc_result_size r = first_zero(a, s);
    if (!r.ok) {
        return fallback;
    }
    return r.value;
}
```

Test a string against a constant with `SC_STR_EQ_LIT`, and check that an
allocation succeeded before you write to it.

```c good
#include <safec/arena.h>
#include <safec/str.h>

/* Copies the request method into fresh arena bytes when it is GET. */
bool keep_get(sc_arena *a, sc_str method, sc_slice *out) {
    if (!SC_STR_EQ_LIT(a, method, "GET")) {
        return false;
    }
    sc_slice copy = sc_arena_alloc(a, method.len);
    if (copy.len != method.len || !sc_slice_copy(a, copy, method)) {
        return false;
    }
    *out = copy;
    return true;
}
```

## When something fails

Read the rule id (`lint-rule R1-no-subscript`), find it in the table above, and
change the construct it names. A conversion finding means an expression mixes
types: check the range and cast on purpose. If a library call returns `false`,
handle that case in your function; do not ignore the result.

A sanitizer report, a failed proof or a red CI step is information about your
code. Fix the code that caused it. Do not change a test, a contract or a
waiver to make it pass. If you cannot tell what is wrong, report what you ran
and what it printed.
