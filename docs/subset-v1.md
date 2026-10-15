# Subset v1

Application code stays inside this subset. `stdlib/` and `adapters/` are outside
lint (Semgrep, clang, and clang-tidy). Unsafe C lives only in `stdlib/`.

Rule ids are the same in this table, `lint/semgrep/subset-v1.yml`, fixture
filenames under `tests/lint/bad/`, and `lint-rule` lines. R3, R5 (macro hides),
and R12 (macro hides) also run on comment-and-string-stripped text in
`scripts/lint.py`.

The compiler flags for R7 and R11 are `-std=c11 -Werror -Wall -Wextra -Wvla
-Wconversion -Wsign-conversion`. R11 maps clang diagnostics whose warning flag
is `-Wconversion`, `-Wsign-conversion`, `-Wfloat-conversion`, or
`-Wimplicit-int-conversion`.

| Rule | Ban | Tool |
| --- | --- | --- |
| R0 | A file under `adapters/` may call a C library outside this subset. List the file in this table. Lint skips `stdlib/` and `adapters/`. | Path exclusion |
| R1-no-subscript | No array subscript. | Semgrep `R1-no-subscript` |
| R2-no-ptr-arith | No pointer arithmetic: `p + n`, `n + p`, `p - n`, `p +=`, `p -=`, `++p`, `p++`, `--p`, `p--`, `*(p + n)`, `*(p - n)`. Integer `i + 1` / `i++` is not this rule. | Semgrep `R2-no-ptr-arith` |
| R3-no-char-ptr | No `char *`, including `const char *` and `char const *`. Comments and strings do not count. | `scripts/lint.py` `R3-no-char-ptr` |
| R4-no-varargs | No `va_start` or `va_arg`. | Semgrep `R4-no-varargs` |
| R5-no-malloc-free | No `malloc`, `calloc`, `realloc`, or `free`, including a macro that hides the call. Comments and strings do not count. | Semgrep `R5-no-malloc-free` |
| R6-no-raw-array | No raw array declaration, including `T a[] = {...}` and `typedef T name[N]`. | Semgrep `R6-no-raw-array` |
| R7-no-vla | No variable length array. | `clang -Wvla` as `R7-no-vla` |
| R8-no-goto | No `goto`. | Semgrep `R8-no-goto` |
| R9-no-errno | No `errno`. | Semgrep `R9-no-errno` |
| R10-no-recursion | No recursion. | `clang-tidy` `misc-no-recursion` as `R10-no-recursion` |
| R11-no-implicit-conversion | No implicit conversion. | `clang` conversion warnings as `R11-no-implicit-conversion` |
| R12-no-alloca | No `alloca` or `__builtin_alloca`, including a macro that hides the call. | Semgrep `R12-no-alloca` |
| tidy-narrowing | No narrowing conversion. | `clang-tidy` `bugprone-narrowing-conversions` as `tidy-narrowing` |

No adapter is listed yet. The HTTP demo's libmicrohttpd adapter is a later row under R0.
