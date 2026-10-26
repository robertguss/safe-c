---
title: "Safe C application stress test (the Postgres explorer)"
created: 2026-10-26
updated: 2026-10-26
type: concept
tags: [safe-dialect, agent-workflow]
sources: []
confidence: medium
---

# Safe C application stress test (the Postgres explorer)

Decision (Robert, 2026-10-25): test Safe C on an application that is not small by building a Postgres explorer in the style of Postico 2 under `examples/explorer/`, in the same repo so the library and the app are fixed together. It is the first Safe C program with a GUI, threads that talk to each other, and a network client library. The HTTP demo had none of these. The plan is `docs/plans/2026-10-25-2038-feat-postgres-explorer-plan.md`. Robert's target is macOS on Apple Silicon; agents build and check it on Linux.

The app itself is not the main result. The main result is the breakage log, `examples/explorer/BREAKAGE.md`: one row for each rule fight, missing module, slow path or workaround, with its cost and how it was resolved. This page summarises that log after each milestone. It extends the SC-18 trial on Mo's `src/base` (`docs/evidence/sc18/mo-base-lint.md`), which ran the lint over existing code. The explorer is written against the subset from the start, so its log counts what writing new code costs, not what porting old code costs.

The fixed choices that shape the log:

- Unsafe code stays in adapters. SDL3, SDL3_ttf, Clay and libpq are reached only through `adapters/`, and the app sends the UI adapter plain structs (rule R0, [[safe-c-dialects]]).
- Threads talk only through `chan` and `mailbox`, the modules whose evidence is tests, ThreadSanitizer, GenMC and TLA+ rather than WP ([[verifier-in-the-loop]]).
- The subset does not change during the build. A rule fight gets a log row, and a rule change needs Robert's approval.

## State after U1 (2026-10-26)

Ten gaps were seeded from planning, before any explorer code existed. The largest:

- A message is a handle, so sending one number means allocating a handle in the sender's table first.
- Handle tables wear out after 65,535 reuses of a slot, and there is no API to renew one.
- There is no blocking wait and no MPMC queue.
- There is no pointer to a [[typed-slices]] element, by design (SC-23).
- There is no append and no number formatting. SC-18 found the same gap. Closed by SC-36: the proved `buf` module went straight into the stdlib rather than an app-side formatter, because any adopter that prints a count hits it.
- There is no subset-clean way to receive `argv`.

Not seeded, found by milestone 2: there is no UTF-8 module, so the input box took ASCII only. Closed by SC-40 (U9): the proved `utf8` module. Its one promise that needs induction (a cut prefix of a valid string is valid) is proved by a ghost walk, since WP cannot prove a recursive logic function is never negative; the walk needs a ghost copy of the length check, because Frama-C forbids ghost code calling real functions.

All ten were open after U1; milestone 2 closed four of them (SC-35, SC-36), and `examples/explorer/BREAKAGE.md` tracks each one. They were found by reading the headers and the subset, not by writing the app, so milestone 3's log may show some of them cost less than expected, and may find others.

## State after M1 (U1–U3) (2026-10-26)

SC-32 (#63), SC-33 (#64) and SC-34 (#65). The explorer is a Postico query-view shell that `make -C examples/explorer` builds, with headless screenshots. The log is `examples/explorer/BREAKAGE.md`.

Two of the ten seeded gaps closed: Clay lives in `adapters/third_party/` and is compiled from `adapters/ui.c` (SC-33); that adapter owns `main` and passes argv to `app_main` as a `sc_tslice` of `sc_str` (KTD17, SC-33). The other eight stay open.

Writing the app added rows. Closed: `ui_arena_new` for arena backing; a flat parent-index tree instead of children arrays or recursion; one shared `ui_frame_valid`; Clay's word-cache cap; a 640 by 400 point minimum window. Still open: copying a whole `ui_node` to read `parent`; copying every shell label into the frame arena each frame. The expected R11 and R3 fights did not show up.

## State after M3 (U9–U12) (2026-10-26)

SC-40 (#74), SC-41 (#75), SC-42 (#76) and SC-43. The explorer connects, streams a query into a virtualised grid with a row-detail pane, and keeps up to nine result tabs on one connection. "Load all" brings in one million rows. The full retrospective is at the end of `examples/explorer/BREAKAGE.md`.

- **Fixed in Safe C:** the missing UTF-8 module became the proved `utf8` module (SC-40). The adapters gained `ui_log` and `sc_clock_us`.
- **Worked around in the app:** there is no seekable reader, so the app keeps row marks per chunk. There are no width tables, so wrapping measures prefixes. There is no pointer to a [[typed-slices]] element, so tabs and results move by value. A custom draw region does not know where it was laid out, so hit testing repeats the layout. Each workaround was cheap in time. Moving a result by value relies on a convention nothing checks: only one copy may be filled at a time.
- **Frame time at one million rows, headless at -O2:** a frame build takes about 0.15 ms. Build plus software render takes 10–11 ms, so rendering dominates and the build cost does not grow with the result.
- **KTD2, the thin-adapter bet, held.** Over milestone 3 the app grew by 2,297 lines to 6,973. Of those, 48% is unit-tested core code. `adapters/ui.c` grew by 50 lines to 893, and `adapters/pq.c` stayed at 481.
- **Bugs:** review and the new integration tests found four bugs that no type caught:
  - a frame arena that filled silently, because `text_new` turns an allocation failure into an empty string;
  - inverted horizontal scrolling;
  - a double ellipsis;
  - a step limit of 64 actions, which broke the link when a large result was rerun.

## Related

- [[safe-c-dialects]]
- [[typed-slices]]
- [[verifier-in-the-loop]]
- [[open-questions-for-safe-c]]
