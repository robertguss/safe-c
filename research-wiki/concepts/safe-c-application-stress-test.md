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

All ten were open after U1; milestone 2 closed four of them (SC-35, SC-36), and `examples/explorer/BREAKAGE.md` tracks each one. They were found by reading the headers and the subset, not by writing the app, so milestone 3's log may show some of them cost less than expected, and may find others.

## Related

- [[safe-c-dialects]]
- [[typed-slices]]
- [[verifier-in-the-loop]]
- [[open-questions-for-safe-c]]
