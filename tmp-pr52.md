SC-52 (plan unit U22, milestone 5): the explorer's SQL editor, which replaces the 64 KiB input box, and the milestone 5 retrospective.

## Before
- SQL went into a flat 64 KiB input box (`input.c`) with O(n) inserts, no selection beyond ⌘A, no undo, no highlighting, and ⌘↩ always ran the whole box. Composition from an IME went nowhere: since U21, Linux IMEs drew no preedit at all.

## After
- **`editor.c`/`.h` (core):** a 1 MiB gap buffer per tab with a cursor and an anchor. Undo keeps 100 steps over a 2.25 MiB ring, so replacing a full buffer with a 1 MiB paste can be undone. Typing groups into one step within 500 ms. The editor moves by code point, word, line and document, and ⇧ with any of those selects. ⌘/ comments lines, ⌘] and ⌘[ indent and outdent them, and Tab inserts two spaces. Composition text stays out of the buffer and the history.
- **`editor_syntax.c` (core):** KTD25 highlighting. Lexer checkpoints are saved every 4 KiB, each with its own copy of the dollar tag. An edit re-lexes only until a later checkpoint's state matches again. This file also finds the statement at the caret (`sqltok_stmt_end`) and works out what a run sends.
- **`view_editor.c` (glue):** draws the line-number gutter and only the visible lines, with horizontal scroll and Postico's keyword and string colours. It also draws the grey band on the current statement, the selection, the underlined composition and the caret. It hit-tests clicks and hands the caret's area to `ui_text_area` for the candidate window.
- **`main.c`:**
  - Running: ⌘↩ and Execute run the selection or the current statement, and ⌥⌘↩ runs everything. A queued run keeps the range it was started on.
  - Clipboard: ⌘C, ⌘X and ⌘V use the U21 clipboard.
  - Pointer: drag selects, a double click selects a word, a triple click a line, and the wheel scrolls the editor. The I-beam and `ui_text_input` are set only when they change, or when a composition is dropped (Esc, a tab switch, focus leaving the editor).
- **Also:** new theme tokens; the definition arena is now sized to the editor; the `editor-light`, `editor-dark` and `editor-compose` scenes; `input.c`, `input.h` and their test are removed. `tabs.c` is not touched: the editor lives in `main.c`'s `query_tab` like the old input box.

## Review
ce-code-review ran six local reviewers (correctness, project standards, testing, maintainability, adversarial and performance); no cross-model peer CLI is installed in this orb. Every finding is fixed:
- **P1 (performance):** after any `$$`, no further checkpoint was saved, because the empty tag kept the scratch arena's stale epoch. Every frame then re-lexed from there, taking 281 ms at 1 MiB. `keep_tag` now re-homes empty tags too, and a test checks that checkpoints continue past a `$$`.
- **P1 (correctness):** a run over 128 KiB failed in the link and read "Not connected". A run over `LINK_SQL_MAX` (127 KiB) is now refused with its own notice, and pq-test sends one of exactly that length.
- **P1 (adversarial):** a queued ⌘↩ ran whatever statement held the caret when it left the queue. The tab now records the range when the key is pressed and re-picks only if the text changed since.
- **P2:**
  - ⌘/ could not uncomment a selection that contained a blank line.
  - The band and the caret recounted line numbers from the top on every frame. Both are now found by walking the visible lines.
  - `editor.c` was 1,186 lines; it is now split into `editor.c` and `editor_syntax.c`.
  - Test gaps: the undo ring's stored bytes, a dollar tag renamed to the same length, ↑/↓, the line ends and the document ends, and the outdent caret. Each new assertion fails with its planted bug. The fuzzer now also sends ⌘/, ⌘] and ⌘[ and a second dollar tag.
- **P3:**
  - A composition left behind on a tab switch.
  - A background tab used stale `standard_conforming_strings`.
  - Dead `editor_seal` and `editor_can_undo`.
  - Misplaced doc comments, the `run_sql` alias, `INPUT_PAD`, and the `editor.h` include note.

## Decisions under the standing go-ahead (each has a BREAKAGE row)
- **Link cap:** `LINK_SQL_MAX` in `link.h`. A run sends at most 127 KiB of the editor's 1 MiB, rather than making every link's SQL arena 1 MiB.
- **App workarounds:**
  - Lexer windows are copied out of the gap buffer and widened when they end mid-token.
  - Each checkpoint has a fixed 64-byte tag slot. A longer tag stops the checkpoints after it (open).
  - Checkpoints, undo steps and tabs are copied by value.
  - The statement at the caret re-splits the whole buffer: 12–24 ms at 1 MiB, per edit or caret move, not per frame (open).
- **Closed:** the U21 Linux preedit row.

## Milestone 5
The retrospective is at the end of `examples/explorer/BREAKAGE.md`, and the research-wiki page `safe-c-application-stress-test` gains a State after M5 section. Highlights:
- The tokenizer is the first proved file outside the stdlib.
- The UI adapter grew by 175 lines; the libpq adapter did not change.
- The app is 16,189 lines, 61% of it core with unit tests.

## Checks
- **Ran, all exit 0:**
  - `make check`.
  - `make -C examples/explorer pq-test` and `pq-test-18`, since `link.h` changed.
  - `make -C examples/explorer screenshots`, with every scene inspected, plus the three editor scenes.
  - The editor fuzzer for about 55 s after each change.
  - The editor, focus, view, query-text and theme tests under ASan and UBSan.
  - `python3 research-wiki/tools/lint.py`: 0 issues.
  - A scratch 1 MiB benchmark: paste 23 ms, typing about 55 µs a key, spans for a visible line about 56 µs.
- **Not run:** `make gates`, `make ci`, and the Gates workflow (Robert's rule).
- **Mac checks owed:**
  - A Japanese IME composition in the editor: the underline, and the candidate window at the caret.
  - ⌘C, ⌘X and ⌘V with the system clipboard.
  - The I-beam over the editor, and trackpad scrolling of the editor.
  - ⌥⌘↩, and the Mac keys ⌥←/⌥→ and ⌘↑/⌘↓.
  - U23's password-field IME check.
