# Subset v1

Application code stays inside this subset. `stdlib/` and `adapters/` are outside lint. Unsafe C lives only in `stdlib/`.

R1 through R12 are the twelve rules. R0 is the FFI exception, in the same table. The nine Semgrep rules are the ids in `docs/evidence/lint/rules.yml`.

The compiler flags for R7 and R11 are `-std=c11 -Werror -Wall -Wextra -Wvla -Wconversion -Wsign-conversion`.

| Rule | Ban | Tool |
| --- | --- | --- |
| R0 | A file under `adapters/` may call a C library outside this subset. List the file in this table. Lint skips `stdlib/` and `adapters/`. | Path exclusion |
| R1 | No array subscript. | Semgrep `no-subscript` |
| R2 | No pointer arithmetic. The prototype pattern also matches `$P++`, which has no C type. | Semgrep `no-ptr-arith` |
| R3 | No `char *`. | Semgrep `no-char-ptr` |
| R4 | No `va_start` or `va_arg`. | Semgrep `no-varargs` |
| R5 | No `malloc`, `calloc`, `realloc`, or `free`. The prototype patterns miss a macro that hides the call, so this rule also matches that call with a regular expression. | Semgrep `no-malloc-free` |
| R6 | No raw array declaration. | Semgrep `no-raw-array` |
| R7 | No variable length array. | `clang -Wvla` |
| R8 | No `goto`. | Semgrep `no-goto` |
| R9 | No `errno`. | Semgrep `no-errno` |
| R10 | No recursion. `bugprone-narrowing-conversions` stays on in the same config and is reported as `tidy-narrowing`, apart from R10. | `clang-tidy` `misc-no-recursion` |
| R11 | No implicit conversion. | `clang -Wconversion -Wsign-conversion` |
| R12 | No `alloca` or `__builtin_alloca`. | Semgrep `no-alloca` |

No adapter is listed yet. The HTTP demo's libmicrohttpd adapter is a later row under R0.
