# Explorer breakage log

Each time Safe C gets in the way of the explorer, it gets one row here: a rule fight,
a missing module, a slow path, or a workaround. This log and the app are the two
things the explorer produces (plan R13, KTD13). The wiki page
`research-wiki/concepts/safe-c-application-stress-test.md` summarises it.

Columns:

- **When:** the date and milestone.
- **Rule, module or tool:** what got in the way.
- **What happened and its cost:** what broke and what the workaround cost.
- **Resolution:** a stdlib fix, an adapter, an app workaround, a proposed subset
  change, or `open`.
- **PR:** where it was resolved.

A subset change needs a row here and Robert's approval first (plan R10).

## Seeded from planning

These ten gaps came out of reading the stdlib and the subset before any explorer code
existed (U1). Each one's resolution is still `open`; the "planned" note is what the
plan expects to do and gets replaced by what actually happened.

| When | Rule, module or tool | What happened and its cost | Resolution | PR |
| --- | --- | --- | --- | --- |
| 2026-10-26 M1 | `handle`, `chan`, `mailbox` | A channel or mailbox message is a handle. Sending one `size_t`, such as a request id, means allocating a handle in the sender's table first. Every message costs an alloc and a free. | open. Planned: the message header is a handle to a record in an arena, and the number goes in the record. | |
| 2026-10-26 M1 | `handle` | Each slot retires after 65,535 reuses, and there is no API to renew a table. A long session runs out. | open. Planned: each thread's table lives in its own arena, which is reset once nothing is in flight. The channel and mailbox storage is never reset. | |
| 2026-10-26 M2 | stdlib | There is no MPMC queue and no shared free list, and `chan` has one reader. Workers cannot take arenas back from a pool themselves. | open. Planned: the UI thread is the only allocator and grants arenas as credits with each request and chunk. | |
| 2026-10-26 M2 | `chan`, `mailbox` | Neither has a blocking wait. A worker must wait on its libpq socket, its request channel and a timer at once. | open. Planned: a self-pipe per worker in the pthreads adapter, polled with the socket. On the UI side, a wake-pending flag and a 100 ms timeout while a query runs. | |
| 2026-10-26 M2 | stdlib | Outside `chan` and `mailbox`, the stdlib has no atomic flag, so a cancel or wake flag has no subset-clean form. | open. Planned: the flag lives in the pthreads adapter. | |
| 2026-10-26 M2 | `make prove` | Threads, file reads and `mmap` are outside WP, so the adapters that do them cannot be proved. | open. Planned: tests, ThreadSanitizer and fuzzing, the same evidence `chan` and `mailbox` use. | |
| 2026-10-26 M1 | `make lint` | Lint scans every `.c` and `.h` under `examples/`, so the Clay header cannot be vendored next to the app. Clay's macros fail the subset. | open. Planned: vendor Clay under `adapters/third_party/`. The app hands `adapters/ui.c` plain node records (KTD2). | |
| 2026-10-26 M3 | `tslice` | `sc_tslice_get` and `sc_tslice_set` copy an element in or out. There is no pointer to an element, so changing one field of a record means copying the whole record twice. This is by design: a pointer would escape the epoch check (SC-23). | open. Planned: keep records small and measure the cost on the million-row grid. | |
| 2026-10-26 M2 | `str`, `slice` | There is no append and no number formatting. Row counts, timings and status text all need both. | open. Planned: an app-side formatter first. Move it to the stdlib if the HTTP demo or another app needs it too. | |
| 2026-10-26 M1 | R1-no-subscript, R3-no-char-ptr | `int main(int argc, char **argv)` and any `argv[i]` read break the subset, so there is no subset-clean way to receive argv. `main.c` declares `main(void)`, as `examples/http/server.c` does. | open. Planned: `adapters/ui.c` owns `main` and passes the arguments as a `sc_tslice` of `sc_str` (KTD17). | |

## Milestone entries

| When | Rule, module or tool | What happened and its cost | Resolution | PR |
| --- | --- | --- | --- | --- |
| 2026-10-26 M1 (U2) | R6-no-raw-array, R5-no-malloc-free | App code has no subset-clean way to get backing memory for an arena: a static buffer is a raw array, and `malloc` is banned. The HTTP demo never hit this because its adapter hands out request arenas. | Adapter: `ui_arena_new` in `adapters/ui.c` mallocs the storage, never frees it, and returns a ready `sc_arena`. Any app needs some version of this; a shared adapter or a stdlib note would help. | SC-33 |
| 2026-10-26 M1 (U2) | R1-no-subscript, R10-no-recursion | A UI tree cannot be an array of children or a recursive walk. | App workaround that turned out cheap: a flat depth-first list where each node names its parent's index. The builder needs no stack (closing a box reads its parent), and the validator needs none either (a node's parent must be the previous node or one of its ancestors). About 130 lines with tests. | SC-33 |
| 2026-10-26 M1 (U2) | R0 boundary | The adapter must not hand Clay a broken tree, so the first version kept its own copy of the frame validator. Review found the two copies already disagreed on font checks, and only the app copy had tests. | Adapter calls the app's tested `ui_frame_valid`, which `sc_ui.h` declares as app-supplied, like `app_main`. One copy, unit-tested. The adapter now trusts app code to validate, which is fine in one process but means a second app must supply the same function. | SC-33 |
| 2026-10-26 M1 (U2) | Clay (vendored), not Safe C | Clay's default word cache (16,384 words) fills after one busy frame, and then every later layout fails. Found in review, reproduced with a 1,000-text stress frame. | Adapter sets the element and word caps before `Clay_MinMemorySize`, and a failed frame is never shown. Logged because the adapter, not the subset, is where this cost landed. | SC-33 |
| 2026-10-26 M1 (U2) | `tslice` | Reading back one node to close a box copies the whole 80-byte `ui_node` (`SC_TSLICE_GET`), only to use its `parent` field. | open, cost not yet measured; a frame is under a few hundred nodes. Revisit with the million-row grid (M3). | SC-33 |
| 2026-10-26 M1 (U3) | `str` | An `sc_str` must point into a live arena, so every label the shell draws ("QUERIES", "Not connected", the ‹ › glyphs) is copied into the frame arena with `SC_STR_LIT` on every frame: about 15 copies a frame for the empty shell. | open. Cheap at this size; a read-only "static string" arena, or a `sc_str` flag for literals the epoch check skips, would remove it. Revisit if a profile shows it. | SC-34 |
| 2026-10-26 M1 (U3) | R11-no-implicit-conversion, R3-no-char-ptr | The plan expected fights here. There were none: the palette is `uint32_t` hex unpacked with explicit casts, and the view code builds `ui_node` values field by field. The cost is verbosity, about 215 lines for the empty shell. | No change needed. | SC-34 |
| 2026-10-26 M1 (U3) | Layout, not Safe C | Review found the shell broke on small windows: the placeholder ran over the sidebar and the bottom bar fell off-screen, because text never clips and the Queries box was a fixed 150 points. | Adapter sets a 640 by 400 point minimum window; the Queries box shrinks first; the bottom bar keeps a gap between its thirds. | SC-34 |
