# Prototype evidence

The prototypes are throwaway. These files are the sources and logs from that run. `wp/mut.c` is included with them. None of the logs is a proof run of `mut.c`.

## What the proof logs print

`wp/wp1.out` parses `arena.c` and prints `Proved goals:   40 / 40`. Its wall time line is `real 0m2.800s`. The prover line names Alt-Ergo 2.6.4.

`wp/wp2.out` parses `arena2.c` and prints `Proved goals:   82 / 82`. Its wall time line is `real 0m5.073s`. The prover line names Alt-Ergo 2.6.4.

`wp/wp2-warm.out` parses `arena2.c`, prints `[Cache] found:62`, and prints `Proved goals:   82 / 82`. Its wall time line is `real 0m4.539s`.

`wp/ghost.out` parses `ghost.c` and prints `Proved goals:   20 / 21`. The unproved goal is `typed_stale_call_slice_get_requires_2`, reported as a timeout. The prover lines name Alt-Ergo 2.6.4 and Z3 4.13.3. Its wall time line is `real 0m12.130s`.

The logs do not record the `frama-c` command line or a Frama-C version. This machine still has no `frama-c` binary, so those commands were not run again here.

`wp/mut.c` defines `slice_get` as a return of `s.ptr[i+1]`. There is no proof log for that file in this directory.

## What the lint logs print

`lint/rules.yml` has nine rules. Their ids are `no-raw-array`, `no-subscript`, `no-ptr-arith`, `no-char-ptr`, `no-malloc-free`, `no-alloca`, `no-varargs`, `no-goto`, and `no-errno`.

`lint/sg.json` names Semgrep 1.181.0. It records a finding for each of those nine ids on `lint/bad.c`. Its `total_time` is 0.25234484672546387 seconds. It also records a partial-parse warning on `bad.c` line 10.

`lint/cc.out` shows four compiler errors on `bad.c`. They are `-Wsign-conversion`, `-Wvla`, and `-Wimplicit-int-conversion`. The file records no wall time.

`lint/tidy.out` shows three warnings treated as errors. They are `clang-analyzer-core.StackAddressEscape`, `misc-no-recursion`, and `bugprone-narrowing-conversions`. The file records no wall time.
