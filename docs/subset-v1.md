# Subset v1

Application code stays inside this subset. `stdlib/` and `adapters/` are outside lint. Unsafe C lives only in `stdlib/`.

R1 through R12 are the twelve rules. R0 is the FFI exception, in the same table. `docs/evidence/lint/rules.yml` was not on this machine. R6, R8, R9, and R12 were chosen in this change.

The compiler flags for R7 and R11 are `-std=c11 -Werror -Wall -Wextra -Wvla -Wconversion -Wsign-conversion`.

| Rule | Ban | Tool |
| --- | --- | --- |
| R0 | A file under `adapters/` may call a C library outside this subset. List the file in this table. Lint skips `stdlib/` and `adapters/`. | Path exclusion |
| R1 | No array subscript. | Semgrep `R1-no-subscript` |
| R2 | No dereference of an addition or a subtraction. | Semgrep `R2-no-pointer-arithmetic` |
| R3 | No `char *`. | Semgrep `R3-no-char-star` |
| R4 | No variadic function. | Semgrep `R4-no-varargs` |
| R5 | No `malloc`, `calloc`, `realloc`, or `free`, including a macro that hides the call. | Semgrep `R5-no-malloc` |
| R6 | No `strcpy`, `strcat`, `sprintf`, `gets`, or `scanf`. | Semgrep `R6-no-unchecked-libc` |
| R7 | No variable length array. | `clang -Wvla` |
| R8 | No `goto`. | Semgrep `R8-no-goto` |
| R9 | No `setjmp` or `longjmp`. | Semgrep `R9-no-setjmp` |
| R10 | No recursion. `bugprone-narrowing-conversions` stays on in the same config and is reported as `tidy-narrowing`, apart from R10. | `clang-tidy` `misc-no-recursion` |
| R11 | No implicit conversion. | `clang -Wconversion -Wsign-conversion` |
| R12 | No `alloca`. | Semgrep `R12-no-alloca` |

No adapter is listed yet. The HTTP demo's libmicrohttpd adapter is a later row under R0.
