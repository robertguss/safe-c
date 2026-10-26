# Brief: the `buf` module

The task card this module was built from (text before the card's Result section).

## SC-36: buf stdlib module, byte builder and number formatting

**Scope:** U5 of the Postgres explorer plan. A proved `buf` stdlib module, so app code can build text such as "182,334 rows", "12 ms" and `UPDATE 3` without `printf`.

- A builder (`sc_buf`) is a destination slice, the number of bytes written to it so far, and a sticky overflow flag. `sc_buf_new` starts one empty and not overflowed.
- Each append writes its bytes right after the ones already written and advances the length by exactly that many. An append that does not fit in what is left of the destination writes nothing, leaves the length alone and sets the overflow flag, so a label that did not fit is detected, never silently cut in the middle of a number. While the flag is set, every append fails the same way, even one that would fit.
- A builder whose destination slice is stale (its arena was reset) refuses every append the same way.
- Appends: raw bytes (`sc_buf_lit`, with `SC_BUF_LIT` for a C literal); a string (`sc_buf_str`) that may live in a different arena from the destination, where a stale string also fails; an unsigned decimal (`sc_buf_u64`: "0", "1000", "18446744073709551615"); a decimal with a comma between each group of three digits counted from the right (`sc_buf_u64_sep`: "999", "1,000", "18,446,744,073,709,551,615"); and a millisecond duration (`sc_buf_ms`).
- Duration rule: under 1000 ms, the milliseconds and " ms" ("0 ms", "999 ms"). Otherwise the duration in tenths of a second, rounded half up (1049 ms is 10 tenths, 1050 ms is 11); while that is under 600 tenths, it prints as seconds with one decimal and " s" ("1.0 s", "1.1 s", "59.9 s"). From 600 tenths on, whole seconds rounded half up, printed as minutes and seconds: "1 min 0 s" for 59,999 and 60,000 ms, "3 min 4 s" for 184,000 ms.
- `sc_buf_view` returns the written bytes as a string in the destination's arena, or the canonical empty string when the destination is stale or nothing has been written.

**Files:** `include/safec/buf.h`, `stdlib/buf.c`, `tests/buf_test.c`, `fuzz/buf_fuzz.c`, `tests/properties/buf/`, `waivers.toml`, `AGENTS.md`, `CHANGELOG.md`.

**Done when:**

- [ ] `0`, `9`, `10`, `999`, `1000` and `UINT64_MAX` format exactly, with and without separators.
- [ ] Durations 0, 999, 1000, 1049, 1050, 59,999 and 60,000 ms format per the rule above.
- [ ] An append with exactly enough room succeeds; with one byte less it writes nothing and sets overflow; a later append that would fit still fails.
- [ ] A builder whose destination is stale refuses every append.
- [ ] The fuzz harness compares decimal output with `snprintf("%llu")` and separator output with a reference that inserts commas.
- [ ] `make prove PROVE_SRC=stdlib/buf.c` reports every goal valid or waived, `make properties` is clean, `make mutate MUTATE_SRC=stdlib/buf.c` has no survivors, and `make package-test` installs the header.
