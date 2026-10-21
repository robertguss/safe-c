# SC-18: the subset lint over Mo's `src/base`

Finding (SC-18, agent, not yet reviewed by Robert). Nothing in robertguss/mo was changed.

## What ran

- Mo: `robertguss/mo` at `b7594b4ede233c64b5be95b4b8446529259a81d5` (`main`, shallow clone), `src/base/`: 21 `.c` and `.h` files plus two `.def` tables (not linted), 2602 lines.
- Safe C: `0c69280` (`main` after PR #29, so R14 and the `--disable-nosem` fix are included; the regex scans are still in `lint.py`, PR #30's move to Semgrep rules is not). I ran it first at `a7beb11` (before #29) and the per-file result was identical, so R14 never fires on Mo: no `#pragma`, `_Pragma`, `NOLINT` or `nosemgrep` in `src/base`. semgrep 1.181.0, clang and clang-tidy 18.1.3.
- Command, from the Safe C tree:

```text
SEMGREP_ENABLE_VERSION_CHECK=0 SAFEC_LINT_CPPFLAGS="-I<mo>/src" \
  python3 scripts/lint.py <mo>/src/base/*.c <mo>/src/base/*.h
```

`scripts/lint.py` prints one `lint-rule` line per file and rule, with no line numbers or counts. The counts below come from `hits.py.txt` beside this report: it calls `lint.py`'s own regex scans and runs the same Semgrep rule file with `--json`, then dedupes by file, line and rule (R5 is matched by both the regex scan and a Semgrep rule). It does not re-run clang or clang-tidy; those raised nothing (R10, R11, R13 and `tidy-narrowing` did not fire in the file-level run). Mo's `limits.h`, `codes.c`, `decimal.h`, `hash.h`, `hostops.h`, `path.h`, `prims.h` and `stack.h` pass.

Result: at file level **13 of 21 files fail and 8 pass**; six rules fire (table below).

## Hits per rule

| Rule | Hits | Files | Group |
| --- | ---: | --- | --- |
| R1-no-subscript | 76 | diag.c 35, hash.c 20, decimal.c 10, path.c 9, buffer.c 2 | right but costly |
| R3-no-char-ptr | 45 | diag.c 15, diag.h 9, buffer.c 9, arena.c 6, codes.h 2, one each in arena.h, base.c, base.h, buffer.h | right but costly; casts and `unsigned char *` are over-broad |
| R2-no-ptr-arith | 19 | diag.c 10, buffer.c 8, arena.c 1 | real, one arguably wrong |
| R9-no-errno | 11 | diag.c 9, stack.c 2 | real |
| R6-no-raw-array | 10 | buffer.c 4, decimal.c 3, arena.c 1, diag.c 1, hash.c 1 | real, one arguably wrong |
| R5-no-malloc-free | 3 | arena.c (`malloc`, `free`, `realloc`) | real, and by design |
| R4, R7, R8, R10, R11, R12, R13, tidy-narrowing | 0 | | |
| **Total** | **164** | | |

### The three groups

**Real violations, by Mo's own design.** R5: `arena.c` is the one file Mo lets call `malloc` ("this is the only code that calls malloc", `arena.h`), so these three hits are the adapter boundary that Safe C expects to be marked as R0 code, not a defect. R9: `diag.c` reads `errno` after `open`, `fstat` and `read` (lines 374-418) and `stack.c` after `sysconf`; those are OS calls, so under Safe C they belong in an adapter that returns an `sc_result`, not in application code. R2: the 10 hits in `diag.c` are one function laying out a single allocation (`(char *)block + labels_size`, lines 158-193, 339-340), and the `buffer.c` ones are the digit-writing loops (`first -= 1`, `end - first`).

**Rule wrong or too broad.** None is a clear false positive like the old R2 case. Three are arguable:

1. R3 is a regex on `char *`, so it also fires on the cast `(char *)block` that exists only to do byte arithmetic (diag.c 158-162, 192-193, 340), and on `unsigned char *` (arena.c 33, 113, 160, 174), which is a byte pointer and not a C string. About a third of the 45 hits are casts and byte pointers. The rule should say which of the two it means; a `const uint8_t *` would pass.
2. R6 fires on `_Alignas(16) max_align_t data[];` (arena.c 15), a flexible array member that is the header of a malloc'd chunk. It is a raw array under the rule's text, and there is no safe spelling, but the rule's text should say flexible array members are in scope.
3. R2 fires on `newline - bytes` (diag.c 308, buffer.c 109, 116, 129): pointer *difference* produces an integer and reads no memory. The rule's text lists `p - n`, not `p - q`; the Semgrep pattern does not tell them apart for bare identifiers.

**Right but costly for code like Mo's.** R1 is 76 of the 164 hits and nearly all of it is array-of-struct access (`diag->fixes[i]`, `source->line_starts[middle]`), SipHash state (`v[0]..v[3]`, hash.c) and 256-bit multiply words (`words[i + j]`, decimal.c). `sc_slice` holds bytes only (`sc_slice_get` returns `uint8_t`), so a table of `struct mo_fix` or of `uint64_t` has no safe form today. Short fixed arrays with constant indices (`v[0]`) are the cheapest to allow. R3 on public `const char *` parameters (`mo_arena_copy_text`, `mo_assert_failed`, `struct mo_code.identifier`) is right, since `sc_str` carries a length, but it touches every function that takes a path or a message.

## Candidate follow-up cards

Each one is a lint or stdlib change and is not part of this PR.

1. **R3: split casts and byte pointers from C strings.** Allow `unsigned char *` and the `(char *)` cast used as an address step, or rename the rule to what it forbids. Needs a fixture under `tests/lint/good/`.
2. **R6: say flexible array members are flagged**, and add a `bad` fixture for one.
3. **R2: do not flag pointer difference.** Either a Semgrep pattern that matches `p - q` of two pointers as allowed, or a documented known gap like `s.ptr + 1`.
4. **Typed arrays.** Either a slice over `uint64_t` and over struct elements, or a documented pattern for constant-indexed fixed state such as SipHash. This is the largest cost: 76 R1 hits and 10 R6 hits.
5. **An `errno` adapter.** A shipped adapter that wraps `open`, `read` and `sysconf` and returns `sc_result_size` would remove R9 (11 hits) without Mo writing its own.
6. **lint.py output.** `lint.py` prints rule IDs per file with no line or count, so a trial like this one needs a side script. A `--verbose` mode that prints `file:line rule` would have made `hits.py.txt` unnecessary.

## Stdlib against Mo's `src/base`

| Mo | Safe C module | Where the APIs differ |
| --- | --- | --- |
| `arena.c`: `mo_arena_*`, malloc'd chunks, a `limit` on total system memory, `resize` in place, alignment up to 4096 | `sc_arena` (`arena.h`) | Safe C's arena is one caller-supplied buffer of fixed capacity, 8-byte alignment, returning an `sc_slice` with a generation check; it never calls `malloc`. Mo's arena grows by chunks, takes a per-call alignment, resizes, and returns `NULL`. A port needs the adapter to own the chunk list and give Safe C one buffer per chunk, or Safe C needs a growable-arena adapter. `sc_arena_reset` matches `mo_arena_free`'s "reuse after free". |
| `buffer.c`: growable byte buffer, `append_*` with a limit | `sc_slice` / `sc_str` over a Safe C arena | Safe C has `sc_slice_copy`, `sc_slice_sub` and `sc_str_*` compares, but no append, no decimal or hex formatting, and no little-endian writer. `mo_buffer_append_int`, `_hex`, `_le16..64` have no counterpart. Failure is `bool` in both; Safe C's `sc_result` could replace it. |
| `limits.h`: ~100 named integer constants | none | Plain `#define` and `enum` constants pass the lint (the file has no hits). Nothing in the stdlib needs to change. |
| `path.c`, `hash.c`, `decimal.c` | none | They need typed arrays (see candidate 4). `path.c` writes into a `char *` out-buffer, which `sc_slice` over bytes covers. |
| `diag.c`, `stack.c` | `sc_result`, `sc_str` | They return `errno`-valued `int`s and `const char *` text; `sc_result_size` and `sc_str` are the equivalents. |

Mo's header comment says the arena is "the only code that calls malloc", which is the same boundary as Safe C's rule R0 for `adapters/`. A port would move `arena.c`'s three hits into an adapter and leave the rest of `src/base` in the subset.
