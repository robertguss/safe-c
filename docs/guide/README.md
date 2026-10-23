# Safe C guide

For a person or an agent adopting Safe C in a C project. Safe C is a strict
subset of C, a small standard library that replaces what the subset bans, and
gates that check both. You write application C inside the subset, link
`libsafec`, and lint with the installed rule pack.

| Page | Read it when |
| --- | --- |
| [Quickstart](quickstart.md) | You are starting: install, a first program, lint, CI. |
| [Subset by example](subset-by-example.md) | Lint rejected something, or you want to know what a rule bans and what to write instead. |
| [Contracts](contracts.md) | You want to know what the library guarantees, or you are writing a module and its ACSL. |
| [Red gates](red-gates.md) | A gate failed and you need to know what it is telling you. |
| [What M1 to M3 showed](what-m1-to-m3-showed.md) | You want the evidence so far, and its limits, before you commit to adopting it. |
| [Agent prompt](../agent-prompt.md) | You are giving an agent the rules: a block to paste into `AGENTS.md` or a system prompt. |

The reference documents are [`docs/subset-v1.md`](../subset-v1.md) for the rule
table, [`AGENTS.md`](../../AGENTS.md) for the gates and toolchain pins, and
[`docs/project-sketch.md`](../project-sketch.md) for the design.

## These pages are tested

Every `c` block in this guide and in the agent prompt is compiled with the
subset's warning set and linted by `make guide-test` (`scripts/guide-check.py`).
A block is tagged with what it is:

| Tag | Checked as |
| --- | --- |
| `c good` | Compiles and passes `safec-lint` with no findings. |
| `c avoid` | The same, for code that is legal but discouraged. |
| `c bad R1-no-subscript` | Compiles, and lint reports that rule. |
| `c adapter` | Compiles; adapters are outside the subset, so lint skips it. |
| `c quote include/safec/slice.h` | Every line is a line of that file, in order. `/* ... */` marks lines left out. |
| `c file=main.c` | A file of the quickstart project, which the check builds. |

The commands in the quickstart's `sh install` and `sh project` blocks run
against a scratch install, so the quickstart is exercised from a clean prefix.
A block with no tag fails the check. `make ci` runs it as the `guide` step of
`scripts/ci.py`.
