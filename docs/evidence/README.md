# Prototype evidence

The prototypes are throwaway. The Safe C plan says they ran on 15 October 2026, between 00:05 and 00:16 EDT, with the ghost experiment at 08:50 EDT, and that they lived in `/workspace/safe-c-proto`.

This machine has no `/workspace/safe-c-proto` directory. A search of the filesystem found no `arena.c`, `arena2.c`, `ghost.c`, `wp1.out`, `wp2.out`, `wp2-warm.out`, `ghost.out`, `bad.c`, `rules.yml`, `cc.out`, `tidy.out`, or `sg.json`. Those files are not in this commit. Stand-in logs would invent the counts, so none were written.

## Toolchain named in the plan

Appendix A names the toolchain below. These versions were not re-checked on this machine.

- Debian 13
- OCaml 5.2.1
- Frama-C 33.0
- Alt-Ergo 2.6.4
- Why3 1.8.2
- Z3 4.13.3
- clang-tidy 18

The plan says Debian 13 has no `frama-c` apt package. It says the opam install took 4 min 31 s on 8 cores. That figure is one run from the plan, not a measurement of this checkout.

## Command named for a rerun

The plan names this command as the live check, run from this file's instructions on `arena2.c`:

```
frama-c -wp -wp-rte -warn-unsigned-overflow arena2.c
```

`frama-c` is not installed here, and `arena2.c` is not in the tree, so the command was not run. The plan does not record the full command line for `arena.c`, the warm rerun, or `ghost.c`. This file does not reconstruct those lines.

## Counts and times copied from the plan

Each figure is one run with no spread, as Appendix A states it. This checkout has no log that confirms it.

- `wp/arena.c` proved 40 of 40 goals in 2.8 s. The plan calls that log `wp1.out`.
- `wp/arena2.c` proved 82 of 82 goals in 5.1 s with the cache off, and 4.5 s on a warm cache. The plan calls those logs `wp2.out` and `wp2-warm.out`.
- A file-scope ghost epoch in `wp/ghost.c` proved 20 of 21 goals in 12.1 s. The plan calls that log `ghost.out`. The unproved goal is the stale call after `arena_reset`. The fresh call proved. The plan says two arenas were not tried.
- Three code mutations failed goals. Allocating `n` instead of the rounded size proved 81 of 82. A `<=` loop bound proved 78 of 82. Reading `s.ptr[i+1]` proved 81 of 83. No contract mutation was tried.
- The plan says Frama-C 33 rejects a ghost field inside a struct.
- clang-tidy 18 caught recursion and narrowing in 0.07 s. Its `cppcoreguidelines-*` checks stayed silent on C.
- Compiler flags caught VLA and implicit conversions. Nine custom Semgrep rules caught the other subset checks in 1.8 s. The plan says the `char *` rule needed `pattern-regex`.

## Lint inputs named by the plan

The plan says the lint prototype held `bad.c`, `rules.yml`, `cc.out`, `tidy.out`, and `sg.json`. None of those files were on this machine, so `docs/evidence/lint/` has nothing to commit.
