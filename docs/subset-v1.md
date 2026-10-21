# Subset v1

Application code stays inside this subset. `stdlib/` and `adapters/` are outside
lint (Semgrep, clang, and clang-tidy). Unsafe C lives only in `stdlib/`.

Rule ids are the same in this table, `lint/semgrep/subset-v1.yml`, fixture
filenames under `tests/lint/bad/`, and `lint-rule` lines. Semgrep cannot parse
macro bodies or pragmas, so R3, R5 (macro hides), R12 (macro hides) and R14 are
also text matches in the Semgrep file that skip comments, strings and character
literals (R14's `NOLINT` counts in comments).

The compiler flags for R7 and R11 are `-std=c11 -Werror -Wall -Wextra -Wvla
-Wconversion -Wsign-conversion`. R11 maps clang diagnostics whose warning flag
is `-Wconversion`, `-Wsign-conversion`, `-Wfloat-conversion`, or
`-Wimplicit-int-conversion`.

| Rule | Ban | Tool |
| --- | --- | --- |
| R0 | A file under `adapters/` may call a C library outside this subset. List the file in this table. Lint skips `stdlib/` and `adapters/`. | Path exclusion |
| R1-no-subscript | No array subscript. | Semgrep `R1-no-subscript` |
| R2-no-ptr-arith | No pointer arithmetic: `p + n`, `n + p`, `p - n`, `p +=`, `p -=`, `++p`, `p++`, `--p`, `p--`, `*(p + n)`, `*(p - n)`. Integer `i + 1` / `i++` is not this rule. Semgrep has no types for struct fields, so the operand must be a bare identifier typed as a pointer; `s.len - pos` on an `sc_str` is not flagged, and neither is arithmetic on a pointer field such as `s.ptr + 1` (field pointers are only reachable through `sc_slice_*`, whose accessors are the supported way). | Semgrep `R2-no-ptr-arith` |
| R3-no-char-ptr | No `char *`, including `const char *` and `char const *`. Comments and strings do not count. | Semgrep `R3-no-char-ptr` |
| R4-no-varargs | No `va_start` or `va_arg`. | Semgrep `R4-no-varargs` |
| R5-no-malloc-free | No `malloc`, `calloc`, `realloc`, or `free`, including a macro that hides the call. Comments and strings do not count. | Semgrep `R5-no-malloc-free` |
| R6-no-raw-array | No raw array declaration, including `T a[] = {...}` and `typedef T name[N]`. | Semgrep `R6-no-raw-array` |
| R7-no-vla | No variable length array. | `clang -Wvla` as `R7-no-vla` |
| R8-no-goto | No `goto`. | Semgrep `R8-no-goto` |
| R9-no-errno | No `errno`. | Semgrep `R9-no-errno` |
| R10-no-recursion | No recursion. | `clang-tidy` `misc-no-recursion` as `R10-no-recursion` |
| R11-no-implicit-conversion | No implicit conversion. | `clang` conversion warnings as `R11-no-implicit-conversion` |
| R12-no-alloca | No `alloca` or `__builtin_alloca`, including a macro that hides the call. | Semgrep `R12-no-alloca` |
| R13-unchecked-result | No read of `.value` on a tagged result or option unless the same function checks its tag first: inside `if (r.ok)` or `if (r.some)`, in the else of `if (!r.ok)`, after `if (!r.ok) { return X; }` where X does not read `.value`, in `r.ok ? r.value : x`, or behind `r.ok &&`. Any read in the error branch or in the else of `if (r.ok)` is flagged, and so is an early-out whose body is anything but one `return`. Write the positive check (`r.ok`), not `r.ok == false` or `!r.ok ? a : r.value`; those forms are flagged too. Prefer the `sc_*_unwrap` accessors, whose contracts make an unchecked call a proof failure. Semgrep has no types here, so the rule matches any struct field named `value` and reads the tags `ok` and `some` through `.` and `->`. | Semgrep `R13-unchecked-result` |
| R14-no-suppression | No `#pragma` or `_Pragma`, and no `NOLINT` marker anywhere in the file, comments included. A pragma can switch off the warnings behind R7 and R11, and `NOLINT` hides a clang-tidy finding. Semgrep runs with `--disable-nosem`, so a `nosemgrep` comment hides nothing. Use include guards, not `#pragma once`. | Semgrep `R14-no-suppression` |
| tidy-narrowing | No narrowing conversion. | `clang-tidy` `bugprone-narrowing-conversions` as `tidy-narrowing` |

## Adapters (R0)

| File | Wraps | Why it is outside the subset |
| --- | --- | --- |
| `adapters/microhttpd.c` (header `adapters/sc_microhttpd.h`) | libmicrohttpd, for the HTTP demo in `examples/http/` | Takes `char *` strings, owns the `malloc`ed arena storage, and uses signals and threads. It hands application code only `sc_str` and `sc_arena`; the header is inside the subset. |
| `adapters/posix.c` (header `adapters/sc_posix.h`) | `open`, `read`, `fstat`, `close` and `sysconf`, for the errno rule R9 | Takes a `char *` path and reads `errno`. It hands application code only `sc_result_size`, `sc_str` and `sc_slice`, with the errno in `err`; the header is inside the subset. |
