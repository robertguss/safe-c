# Safe C for agents (working name): project sketch

*Robert Guss, October 2026, draft*

## 1. Goal and thesis

**Goal:** make C a practical, close-to-provably-safe language for AI coding agents to write. We don't build a new language. We use a strict subset, a safe standard library, mandatory tooling, and machine-checked specs.

**Thesis:** strict C has always been possible. People rarely write it because the rules are tedious and proof annotations are expensive. Agents take on that tedium cheaply: they don't get tired of bounds checks, ACSL contracts, or fuzz harnesses. C still has the best reach (toolchains, platforms, FFI, existing libraries), and its verification tools (Frama-C, CBMC, TLA+, Lean) are mature enough to run in CI. Put agents and verification together and the old trade-off, where C is either fast and unsafe or safe and impractical, starts to give way.

## 2. Layers

**(a) A strict subset, enforced mechanically.** App code can't use raw arrays, `char*` strings, pointer arithmetic, raw `malloc`/`free`, varargs, or implicit conversions. clang-tidy checks and Semgrep rules enforce this, and a violation fails the build. Unsafe constructs are allowed only inside the stdlib, behind audited, specified interfaces.

**(b) A safe stdlib.**
- Slices and strings that carry their length, with checked access.
- Arena allocators as the default way to manage lifetimes.
- Tagged unions and `Result`/`Option` types in place of sentinel returns and errno.
- Generation-checked handles in place of long-lived pointers, so a stale reference fails safely.

**(c) A default test loop.** Dev builds run with ASan and UBSan. Every module gets a libFuzzer harness. Concurrent code runs under TSan. Valgrind serves as a second opinion. Clang Static Analyzer (or Infer) runs on every change.

**(d) Concurrency model.** Share nothing by default. Each core runs one event loop. Channels pass ownership along with each message, so the sender loses access. The rare state that has to be shared lives behind lock-wrapped types that you can't reach without taking the lock.

**(e) Verification.**
- **Frama-C WP** checks ACSL specs that agents write, in CI. The main targets are absence of runtime errors and functional contracts for the stdlib. **CBMC** is an alternative or complement where bounded model checking fits better.
- **TLA+** covers concurrency and protocol design: channels, event-loop handoff, and shutdown.
- **Lean** handles deep proofs of core library properties. A possible later path is generating C from proven Lean code.

**(f) Optional runtime safety with Fil-C.** Fil-C is an actively maintained memory-safe implementation of C/C++ that uses GC and capability checks. It offers a build mode that catches any remaining memory errors at runtime, at a performance cost. This is useful for code that isn't fully proven, and when linking legacy libraries.

**(g) Later: a safe web layer.** An HTTP layer on h2o or libmicrohttpd, with one arena per request so that request memory is freed in one step and nothing leaks across requests.

## 3. Agent workflow

1. The agent writes the code **and** its specs (ACSL contracts, TLA+ changes where relevant, fuzz harness).
2. Gates run in order: **lint** (subset rules), **build and sanitizer tests**, **fuzz** (time-boxed in PRs, longer runs nightly), **prover** (Frama-C WP or CBMC goals all discharged).
3. Work counts as done only when every gate passes. The agent fixes failures itself. A human reviews specs more than code, because the specs carry the intent.
4. Proof waivers are explicit, tracked, and visible in review.

## 4. Milestones

- **M1: proven core.** An arena allocator plus the slice/string library, verified with Frama-C WP, with a fuzz harness and the first set of lint rules working in CI.
- **M2: types and concurrency.** Result types, generation handles, and ownership-transferring channels, with a TLA+ model of the channel and event-loop protocol.
- **M3: demo.** A small HTTP server on the stack using per-request arenas, to test whether the subset is ergonomic.
- **M4: packaging and docs.** Installable library, rule packs, CI templates, an agent-facing style guide and prompts, and a written write-up.

## 5. Risks and open questions

- **Proof cost:** how much spec and proof effort is needed per line of code, even with agents writing it?
- **Solver speed:** WP and SMT times could make CI too slow. We may need caching, incremental proving, or proving only the stdlib.
- **Subset too restrictive:** real code may need escape hatches. Where do they live, and how are they audited?
- **Model/code drift:** TLA+ models can fall out of step with the C code. Options include conformance tests or trace checking.
- **Agent skill at ACSL:** how reliably can current agents write specs that are strong *and* provable, and not just weak specs that pass trivially?
- **Interop with unsafe C libraries:** wrapping OpenSSL, libc, and similar libraries safely, and deciding how far guarantees extend across the FFI boundary.

## 6. Prior art to study

MISRA C and CERT C (coding standards); Fil-C (memory-safe C runtime); CHERI (hardware capabilities); seL4 (verified kernel); CompCert (verified C compiler); Frama-C (WP, Eva, ACSL); VeriFast (separation-logic verification of C); CBMC (bounded model checking); Astrée (sound static analysis for absence of runtime errors); Zig's safety model (checked builds, explicit allocators, slices); and recent research on using LLMs to generate specs and proofs (ACSL, Dafny, Lean, Verus).
