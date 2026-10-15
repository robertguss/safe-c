# Prototype evidence

The prototypes are throwaway. These files are the sources and logs from that run.

`wp/mut.c` is a seeded mutation of `wp/arena2.c`, not a second arena implementation. The files are identical except `slice_get`: `arena2.c` returns `s.ptr[i]`; `mut.c` returns `s.ptr[i+1]` while the ACSL `ensures` clause still claims `\result == s.ptr[i]`. None of the logs is a proof run of `mut.c`.

## What the proof logs print

Every WP log starts with the same two skipped RTE guards, then annotates functions:

```
[wp] Warning: Skipped RTE guards: unaligned pointers (\aligned not supported)
[wp] Warning: Skipped RTE guards: invalid function pointer calls (\valid_function not supported)
```

RTE annotation ran. Those two guard classes were then dropped. The proved-goal counts below are for goals WP scheduled after the skips, not full runtime-error discharge. Alignment matters on this prototype: `arena_alloc` sets `s.ptr = a->base + a->used`.

`wp1.out` also prints a `wp:pedantic-assigns` warning on `arena_alloc` (no `assigns \result \from ...`). `ghost.out` prints the same class of warning on `stale` and `fresh`.

`wp/wp1.out` parses `arena.c` and prints `Proved goals:   40 / 40`. Its wall time line is `real 0m2.800s`. The prover line names Alt-Ergo 2.6.4.

`wp/wp2.out` parses `arena2.c` and prints `Proved goals:   82 / 82`. Its wall time line is `real 0m5.073s`. The prover line names Alt-Ergo 2.6.4.

`wp/wp2-warm.out` parses `arena2.c`, prints `[Cache] found:62`, and prints `Proved goals:   82 / 82`. Its wall time line is `real 0m4.539s`.

`wp/ghost.out` parses `ghost.c` and prints `Proved goals:   20 / 21`. The unproved goal is `typed_stale_call_slice_get_requires_2`, reported as a timeout (Qed 3ms, Alt-Ergo). That is the intended stale-call rejection: `stale` resets the arena, then calls `slice_get` with a ghost birth epoch from before the reset. The prover lines name Alt-Ergo 2.6.4 and Z3 4.13.3. Its wall time line is `real 0m12.130s`.

The logs do not record the `frama-c` command line or a Frama-C version. This machine still has no `frama-c` binary, so those commands were not run again here.

## What the lint logs print

`lint/rules.yml` has nine rules. Their ids are `no-raw-array`, `no-subscript`, `no-ptr-arith`, `no-char-ptr`, `no-malloc-free`, `no-alloca`, `no-varargs`, `no-goto`, and `no-errno`.

`lint/sg.json` names Semgrep 1.181.0. It records a finding for each of those nine ids on `lint/bad.c`. Its `total_time` is 0.25234484672546387 seconds. It also records a partial-parse warning on `bad.c` line 10. Every finding’s `fingerprint` and `lines` fields are the literal string `requires login`. That is Semgrep OSS redaction in this export, not a secret and not reconstructed here.

`lint/cc.out` shows four compiler errors on `bad.c`. They are `-Wsign-conversion`, `-Wvla`, and `-Wimplicit-int-conversion`. The file records no wall time. Paths in that file are `bad.c`.

`lint/tidy.out` shows three warnings treated as errors. They are `clang-analyzer-core.StackAddressEscape`, `misc-no-recursion`, and `bugprone-narrowing-conversions`. The file records no wall time. The original log paths were `/workspace/safe-c-proto/lint/bad.c` (the machine that produced the prototype). Those paths were rewritten in this tree to `docs/evidence/lint/bad.c`. The diagnostics still match committed `bad.c`.
