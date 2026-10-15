# Safe C

*Robert Guss, October 2026*

Safe C is a standalone C library. It makes C a practical language for an AI agent to write close to provably safe. Any C project can adopt it. Safe C ships a strict subset, a safe standard library, and four gates. The gates are lint, sanitizers, fuzz, and Frama-C WP.

Work is done only when `make gates` passes in CI and every proof gap is a listed, unexpired waiver in `waivers.toml`.

## Goal

Strict C has always been possible. People rarely write it, because the rules are tedious and proof annotations are expensive. An agent can repeat bounds checks, ACSL contracts, and fuzz harnesses without tiring of them. C still reaches the toolchains, platforms, and libraries that projects already depend on. Frama-C and TLA+ are mature enough to run in CI. Safe C puts the agent and those checkers on the same change.

## What the stack builds

The stack is linear, from the phase 0 rules through the HTTP demo.

Phase 0 is three files. `AGENTS.md` states the done rule above. `docs/subset-v1.md` is one table of twelve rules, and each row names the tool that enforces it. `waivers.toml` has the waiver schema and no entries. The lint gate fails the build when application code breaks a rule. Unsafe C stays in the standard library. A file under `adapters/` may call a C library outside the subset. The rule table lists that file, and lint skips it by path.

Sanitizer tests and fuzz come next. Tests build with ASan and UBSan. Each module has a libFuzzer harness. A pull request fuzzes for a short budget. The nightly run uses a longer budget.

The prover gate runs Frama-C WP, with runtime-error checks, on the standard library. A goal that is neither proved nor waived fails the build. The waiver report prints every waiver and fails when one has expired. CI runs `make gates`. Valgrind and the Clang Static Analyzer are advisory and do not fail CI.

The first proved code is a bump arena and a slice type, in one change. A slice carries a pointer and a length. Arena allocation returns a slice. Strings are byte slices, with equality, search, and construction from a literal. Each string function has a contract that states the result contents. `Result` and `Option` are tagged structs. Reading the value without checking the tag fails lint.

The concurrency work starts as a TLA+ model of a bounded channel. Send transfers ownership. TLC checks the model only. Generation-checked handles are a fixed table of slots. A handle is an index plus a generation. The channel implementation follows the model, passes ThreadSanitizer, and stays outside WP. It adds no waiver.

The HTTP demo serves `GET /` through a libmicrohttpd adapter. Each request gets one arena. The demo resets that arena after the response, and it parses headers into strings. The adapter is the FFI exception, and the rule table lists it.

## Deferred

Each item below has no task.

- Lean proofs and Lean-to-C generation
- Fil-C as a build mode
- CHERI
- CBMC as a peer prover that can pass the gate
- One event loop per core
- Lock-wrapped shared state
- The FFI story for OpenSSL and libc
- Infer
- h2o
- The agent style guide and prompts
- The unchecked escape hatch
- Adopter pilots
