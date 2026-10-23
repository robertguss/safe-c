# Brief: the `handle` module

The task cards this module was built from (text before each card's Result section) and the Before/After of the PR that shipped it.

## SC-09: Add generation-checked handles

**Scope:** A fixed-capacity handle table in an arena. A handle is `{index, generation}`; `sc_handle_get` returns `SC_OPTION` and checks the generation. A slot whose generation would wrap retires. Needs SC-05 and SC-07.

**Files:** `include/safec/handle.h`, `stdlib/handle.c`, `tests/handle_test.c`, `fuzz/handle_fuzz.c`.

**Done when:**

- [x] Tests cover alloc, free, stale get, reuse, wrap and a full table.
- [x] A freed handle returns none on the next get.
- [x] `stdlib/handle.c` proves N of N goals with no new waivers.
- [x] `sc_handle_get` latency from a committed benchmark is recorded (no floor).

## PR #15 Before and After

Before: Safe C has arenas, slices, strings and Result/Option, but no way to refer to an object that can be freed and reused. The only handle experiment was the ghost-token prototype, which timed out in WP.

After: `include/safec/handle.h` adds a fixed-capacity handle table carved from one arena allocation. `sc_handle_alloc` issues `{index, generation, epoch, table}` for the lowest free slot. `sc_handle_free` moves the slot's generation on, so every copy of the handle goes stale. `sc_handle_get` returns `SC_OPTION` (`sc_option_size`, the slot index) and checks the arena epoch, the table, the slot state and the generation. A slot freed at generation 65535 retires for good instead of wrapping.
