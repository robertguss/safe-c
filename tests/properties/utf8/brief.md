# Brief: the `utf8` module

The task card this module was built from (text before the card's Result section).

## SC-40: utf8 stdlib module

**Scope:** U9 of the Postgres explorer plan. A proved `utf8` stdlib module, so app code can validate, decode and safely cut UTF-8 bytes from the database and from typed text. The explorer's input box switches from ASCII to UTF-8, and its result view shows each invalid byte as U+FFFD.

- Valid UTF-8 follows RFC 3629. A continuation byte is 80 to BF. One byte: 00 to 7F. Two bytes: C2 to DF, then one continuation byte. Three bytes: E0 then A0 to BF; E1 to EC then a continuation byte; ED then 80 to 9F; EE to EF then a continuation byte; each followed by one more continuation byte. Four bytes: F0 then 90 to BF; F1 to F3 then a continuation byte; F4 then 80 to 8F; each followed by two more continuation bytes. Anything else is invalid, so overlong forms (C0 80, E0 80 80), surrogates (ED A0 80) and code points above U+10FFFF (F4 90 80 80) are invalid.
- `sc_utf8_next(a, s, i)` reads the sequence that starts at byte `i` of string `s`. A valid sequence gives its code point, its length (1 to 4) and the valid flag. An invalid one, including a sequence cut short by the end of `s`, gives length 1 (the one byte to skip), code point 0 and no valid flag. With `i` at or past the end of `s`, or a stale `s`, it gives length 0, code point 0 and no valid flag. It never reads past `s`.
- `sc_utf8_valid(a, s)` is true when `s` is live and a walk from the front with `sc_utf8_next` meets only valid sequences. A live empty string is valid; a stale one is not.
- `sc_utf8_count(a, s)` is the number of steps that walk takes: the code points, with each invalid byte counted as one. A stale string counts 0.
- A boundary of `s` is offset 0, or an offset whose byte is not a continuation byte.
- `sc_utf8_prev(a, s, i)`, for backspace and the left arrow: the largest boundary `j` with `i - 4 <= j < i`, or `i - 1` when none of those four is a boundary. It is 0 when `i` is 0, when `i` is past the end of `s`, or when `s` is stale.
- `sc_utf8_truncate(a, s, max)`: `s` itself when `max` is at least its length; otherwise the prefix of length `k`, the largest boundary with `max - 3 <= k <= max`, or `max` when none of those is a boundary. So a valid string is never cut inside a sequence, and its prefix is valid. The prefix shares `s`'s pointer and epoch. A stale `s` gives the canonical empty string.

**Files:** `include/safec/utf8.h`, `stdlib/utf8.c`, `tests/utf8_test.c`, `fuzz/utf8_fuzz.c`, `tests/properties/utf8/`, `waivers.toml`, `examples/explorer/input.c` and its test, the explorer's display copies, `AGENTS.md`, `CHANGELOG.md`.

**Done when:**

- [ ] `A`, `é`, `€` and `😀` each decode to the right code point and length.
- [ ] Overlongs, a surrogate, a code point above U+10FFFF and a lone continuation byte are invalid with skip length 1; a sequence cut short at the end is invalid and nothing past the string is read.
- [ ] Truncating `aé€` to 3 bytes gives `aé`, and to 1 byte gives `a`; the previous boundary from the end of `a😀` is 1.
- [ ] In the input box, typing `é` then backspace removes both bytes, and ← steps over `😀` as one character.
- [ ] The fuzz harness checks validate against a reference decoder written from RFC 3629, and that truncate of a valid string always validates.
- [ ] `make prove PROVE_SRC=stdlib/utf8.c` reports every goal valid or waived, `make properties` is clean, `make mutate MUTATE_SRC=stdlib/utf8.c` has no survivors, and `make package-test` installs the header.
