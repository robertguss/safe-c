# Reading a red gate

`make gates` runs its stages in a fixed order: application lint, sanitizer
tests, ThreadSanitizer tests, the sanitizer and fuzzer canaries, short fuzzing,
Frama-C WP with the waiver check, and the TLC model. **Fix the first red
stage.** Later stages often fail as a consequence of an earlier one. CI runs
more than `make gates`: `scripts/ci.py` lists every step, and `make ci` runs
the list locally (`make ci-docker` inside the pinned toolchain). Green on that
list is the bar.

An adopter runs fewer of these. Lint comes with the installed rule pack. The
rest are the library's own evidence, and apply when you change Safe C itself
or write a module in its style.

## Lint

```text
fail main.c
lint-rule R1-no-subscript
```

`python3 scripts/lint.py --verbose main.c` adds a `main.c:5 R1-no-subscript` line for each finding, so you can go
straight to the line.

The rule id maps to a row in [`subset-v1.md`](../subset-v1.md), and
[Subset by example](subset-by-example.md) shows the break and the fix for each
rule. A `tidy-narrowing` or `R11-no-implicit-conversion` finding comes from the
compiler or clang-tidy: run the file through `clang -std=c11 -Wall -Wextra
-Wconversion -Wsign-conversion -fsyntax-only` to see the exact expression.
`error: missing required tool` means Semgrep, clang or clang-tidy is not on
`PATH`; lint fails closed rather than skipping a check.

Do not silence a finding with a macro or a cast that hides the construct. R3,
R5 and R12 also scan for the banned construct behind a macro. If the code
needs something the subset bans, it belongs in an adapter (R0) with a row in
the rule table.

## Sanitizer and fuzz failures

Tests build with `-fsanitize=address,undefined -fno-sanitize-recover=all`, so
the first undefined behavior stops the run.

```text
ERROR: AddressSanitizer: heap-buffer-overflow ...
runtime error: signed integer overflow ...
```

Rerun the single failing test, or the saved crash input, to get a stack trace
(`build/fuzz/` holds crash artifacts). A fuzz crash input becomes a unit test
case before the fix, so the bug stays caught. The sanitizer canaries
(`make gate-selftest`) are deliberate bugs the gate must catch; a red canary
means the gate itself is broken, not your code.

### Replaying a nightly fuzz crash

The nightly workflow (`.github/workflows/nightly.yml`) fuzzes each harness in its
own job and keeps its corpus in the Actions cache between runs. A crash, leak,
timeout or OOM turns that job red and uploads the input as the
`fuzz-crashes-<harness>-<run id>` artifact. Download and unzip it, build the
matching harness, and run it on the saved file:

```bash
FUZZ_SECONDS=1 python3 scripts/sanitize.py fuzz fuzz/str_fuzz.c
build/fuzz/str_fuzz path/to/crash-<hash>
```

The first command builds `build/fuzz/<harness>`; the second runs the input once
and prints the sanitizer stack trace.

ThreadSanitizer runs every test that includes `<pthread.h>`. A race report
names both accesses and the thread that made each. For the channel, read the
ordering notes in [`include/safec/chan.h`](../../include/safec/chan.h) and the
channel's TLA+ model before you change a store or a load; for the mailbox, read
[`include/safec/mailbox.h`](../../include/safec/mailbox.h) and `models/mailbox.tla`.

## Proof

`make prove` writes `build/prove.log`.

```text
waived sc05-arena-rte-unaligned: stdlib/arena.c :: <translation-unit> :: wp-rte-unaligned
unproved stdlib/slice.c :: sc_slice_sub :: <goal name> (timeout)
prove: 203 proved, 4 waived, 1 unproved
```

A line starting `unproved` is the failure. It names the file, the function and
the goal; the goal name carries the clause that failed (`ensures`,
`loop_invariant`, `assigns`, or `requires` at a call site), and the
parenthesis is the prover's verdict. A `waived` line matched an exact entry in
`waivers.toml`. Strengthen in this order: the loop invariant, then a helper's
contract, then the function's `requires`.

- **A call-site `requires` goal** means a caller does not establish a
  precondition: it called `sc_option_size_unwrap` without proving `o.some`, or
  passed a slice that is not known to belong to the arena. Fix the caller.
- **A timeout is a real failure.** Simplify the invariant or split the
  function before you raise the timeout.
- **An error that names a proof warning** means Frama-C reported something the
  gate does not accept (for example a cast WP's memory model rejects). The gate
  fails closed on warnings.
- **No goals reported** is also a failure: an empty report proves nothing.

## Waivers

```text
error: expired waivers: sc05-arena-rte-unaligned
```

A waiver is a proof gap with an id, a file, a function, a goal, a reason, an
owner and an expiry date. An expired one fails the gate. Either the gap closes
or the owner extends the date; neither is an agent's call, so report it. A new
waiver is also the owner's decision: propose it with what you tried, and do not
add one to get green. `make waivers` prints the register.

## Mutation

```text
survived stdlib/slice.c:42 [arith + #1]
```

`make mutate` plants one code mutation at a time and requires WP to reject it.
A survivor means a mutant that still proves, so the contract does not pin down
that line. Tighten the contract. If the mutant changes no behavior, list it in
`tests/mutate-equivalent.toml` with the reason.

## Model

```text
model: QueueSize=1: FAIL (exit N): ...
```

`build/model/queue-N.log` names the violated property and prints the trace that
reaches it. [`models/README.md`](../../models/README.md) says what each action
and property means and which C function an action maps to. TLC checks the
model, not the C, so a model failure points at the design and a C-side fix has
to keep the table in that README true.

## CI only

When CI is red and your machine is green, download the `gates-*` artifact
(`build/prove.log`, `build/gates.log`, `build/ci/*.log`) and read the log
there. A pinned tool version that differs from yours is the usual cause: CI
pins Frama-C 33.0, Alt-Ergo 2.6.4, Why3 1.8.2, Z3 4.13.3, Semgrep 1.181.0 and
clang 18.
