# What M1 to M3 showed

Safe C's first eleven tasks, SC-01 to SC-11, ran in three milestones between
15 and 18 October 2026: a proven core (M1), types and concurrency (M2) and a
demo (M3). This is what they showed, in the order a prospective adopter would
ask. The design reasons are in the research wiki's
[`acsl-patterns-arena-slice`](../../research-wiki/concepts/acsl-patterns-arena-slice.md)
page; the numbers come from the logs named below.

## M1: a small safe library can be proved

Frama-C WP proves a bump arena and a slice library in seconds. The prototype's
arena proved 40 of 40 goals in 2.8 seconds, and a version that adds slice
access proved 82 of 82 in 5.1 seconds
([`docs/evidence/wp/`](../evidence/README.md)). A seeded bug (`s.ptr[i+1]`
under a contract that still claimed `s.ptr[i]`) broke the proof, so the
contracts say something.

Two things did not work the first way, and both changed the design:

- **A ghost epoch did not catch a stale slice.** The first idea was a ghost
  field that changes on reset. Frama-C 33 rejects a ghost field inside a
  struct, and a file-scope ghost accepted a stale slice when its caller
  recaptured the token after the reset. SC-05 used a runtime epoch in each
  arena and slice instead, which every operation checks and WP proves.
- **Frama-C 33 cannot state alignment.** `\aligned` is not supported (SC-17
  rechecked 32.0, 32.1 and 33.0). The contracts restate it as offset
  arithmetic, and the two RTE guards Frama-C skips for each proved file are
  waivers in `waivers.toml`. An "N of N goals
  proved" total is therefore not full runtime-error coverage, and the gate
  records the skipped guards as goals of their own.

## M2: the hard parts needed different evidence

Result and option types, generation-checked handles and an ownership-passing
channel followed. CI proves every generated goal in the proved modules,
with only the skipped-guard waivers: the arena's 84 goals, the slice
module's 129 and the handle table's 245 are recorded in
[`docs/evidence/sc05/`](../evidence/sc05/prove.log) and the research wiki, and
`build/prove.log` in each CI run has the current totals.

- **Self-review found a real bug.** For the handle table, review showed that a
  public slice copy could roll a generation back and revive a stale handle, so
  the slot bytes stay out of the slice API. Without the epoch and the table's
  offset in each handle, a handle from before a reset or from a sibling table
  matched a new table at the same slot, so a handle carries both.
- **The channel is outside WP.** Threads and atomics are not in WP's model.
  Its evidence is unit tests, ThreadSanitizer, a fuzz harness and a TLA+ model
  of the protocol, with a planted bug for each property that TLC must find.
  TLC checks the design, not the C. The many-to-one mailbox added for the Mo
  scheduler is covered the same way.
- **A weak contract can prove.** `make mutate` (SC-14) plants code mutations
  across the proved files and requires WP to reject each. Survivors that change
  no behavior are listed with reasons. It cannot find a property the
  contract never states.

## M3: the subset is workable, with friction

The HTTP demo ([`examples/http/`](../../examples/http/README.md)) is a usage
test of the subset: a server whose parser and handler are application C, with
one adapter for libmicrohttpd. It served 100,000 requests with no memory growth
([`docs/evidence/sc11/bench.md`](../evidence/sc11/bench.md)). Throughput there
was limited by the Python client, so 3,819 requests per second is a floor, not
a limit, and perf numbers are recorded, not gated.

Writing it showed five frictions:

1. Constants were not free. With no `const char *` and no static tables, every
   request copied `"GET"` into its arena. SC-13 added `sc_str_eq_lit`, which
   compares in place.
2. Scanning is verbose but safe: every byte read is a checked call, so a stale
   request head becomes a 400 and not a crash.
3. A raw array is banned, so there is nowhere to keep a header table. The
   parsed request holds one validated slice and rescans it.
4. Lint had never compiled an application file, so the first `#include
   <safec/...>` failed. Lint now passes the include paths.
5. R2 gave a false positive on `s.len - pos`. SC-13 narrowed it to bare
   identifiers.

## What this does not show

- **No adopter had used it yet.** At M1–M3 the only application code was the
  demo and the sample adopter. The pilot with Mo still waits until Mo's Phase 2
  (target 20 November). The Postgres explorer later became the main application
  stress test (`examples/explorer/BREAKAGE.md`).
- **Application code was not proved then.** WP covered the library; the demo's
  parser had tests, fuzzing and lint. The explorer's `sqltok.c` is now proved
  outside the stdlib.
- **One demo is one data point about ergonomics.** The frictions above are what
  one server hit, and a different program may hit others.
- **Proof strength is bounded.** A contract can be weaker than the intent,
  mutation testing checks it only against the code it covers, and WP's skipped
  guards are waived, not proved.
