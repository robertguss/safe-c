# Brief: the `result` module

The task cards this module was built from (text before each card's Result section) and the Before/After of the PR that shipped it.

## SC-07: Add Result and Option types

**Scope:** `SC_RESULT(T, E)` and `SC_OPTION(T)` as tagged structs whose accessors require the tag in ACSL, plus lint rule R13 that flags reading `.value` without a tag check. Builds on SC-04 only, so it can run alongside SC-06.

**Files:** `include/safec/result.h`, `tests/result_test.c`, `lint/semgrep/subset-v1.yml`, `docs/subset-v1.md`, `tests/lint/bad/R13-unchecked-result.c`.

**Done when:**

- [x] Tests cover ok, err and none cases, and the lint self-test includes R13.
- [x] Reading `.value` on an error result fails lint, and the accessor contract fails WP.
- [x] A caller that checks the tag proves N of N goals.

## PR #13 Before and After

Before: a function that can fail returned `bool` plus an out-parameter, and nothing stopped a caller from reading a value that was never set. There was no lint rule for it.

After: `SC_OPTION(T)` and `SC_RESULT(T, E)` give tagged structs, and the shipped `size_t` pair has accessors whose ACSL `requires` the tag. A caller that skips the check fails the call-site goal. Lint rule R13 flags the same mistake in application code that reads `.value` directly.
