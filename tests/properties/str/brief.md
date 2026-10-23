# Brief: the `str` module

The task cards this module was built from (text before each card's Result section) and the Before/After of the PR that shipped it.

## SC-06: Add proven strings

**Scope:** `sc_str` as a byte slice, with `sc_str_eq`, `sc_str_find` and `sc_str_from_lit`. Each contract states the result content, not just safety.

**Files:** `include/safec/str.h`, `stdlib/str.c`, `tests/str_test.c`, `fuzz/str_fuzz.c`.

**Done when:**

- [x] Tests cover empty, equal, unequal, found and not-found cases.
- [x] `stdlib/str.c` proves N of N goals with no new waivers. *(Amended: two skipped-guard waivers added, see Result.)*
- [x] An `ensures \true` mutation of a string contract fails the prover.
- [x] Prover time is recorded in the PR (no floor).

## PR #12 Before and After

Before: the library had arenas and byte slices but no string type, so callers compared and searched raw slices by hand.

After: `sc_str` (a byte slice) with `sc_str_from_lit`, `sc_str_eq` and `sc_str_find`. Each contract states the result content: `eq` holds exactly when both strings are live and byte-equal, and `find` holds exactly when some offset matches and returns the least one.

## SC-13: Fix the subset frictions SC-11 found

**Scope:** SC-11 found that constant strings like `"GET"` and `"HTTP/1.1"` are copied into the request arena on every request, because `sc_str_eq` needs both strings in one arena, and that lint rule R2 flags `s.len - pos` on an `sc_str` as pointer arithmetic. Add a proven stdlib way to compare against a literal without an arena copy, narrow R2 so the false positive passes while real pointer arithmetic still fails, and move the HTTP parser onto the new call. See the SC-11 section of `research-wiki/concepts/acsl-patterns-arena-slice.md`.

**Files:** `include/safec/str.h`, `stdlib/str.c`, `tests/str_test.c`, `fuzz/str_fuzz.c`, `lint/semgrep/`, `tests/lint/` fixtures, `examples/http/parse.c`, `docs/subset-v1.md`.

**Done when:**

- [ ] The new function has an ACSL contract that states the result, WP proves it N of N, and it has tests and a fuzz harness.
- [ ] The parser no longer copies its constants into the request arena; `http_parse_test` and `make -C examples/http smoke` still pass.
- [ ] A lint fixture shows `s.len - pos` passing and a real pointer subtraction failing.
- [ ] Any new waiver is only the two standard skipped-guard ones; `make gates` is green in CI.

## PR #21 Before and After

Before: comparing a string to a constant such as `"GET"` meant copying the constant into the request arena on every request, because `sc_str_eq` needs both strings in one arena. Lint rule R2 also flagged `s.len - pos` on an `sc_str` as pointer arithmetic whenever a pointer variable named `len` was in scope.

After: `sc_str_eq_lit` (macro `SC_STR_EQ_LIT(a, x, "GET")`) compares a live string with a literal in place, with an ACSL contract that states the result. The HTTP parser and server use it, so the parser takes `const sc_arena *` and no longer has a 500 path. R2 only treats a bare identifier as a pointer operand, so `s.len - pos` passes and `p - n` still fails.
