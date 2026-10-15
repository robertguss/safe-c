---
title: "Runtime memory-safety enforcement"
created: 2026-10-14
updated: 2026-10-14
type: concept
tags: [runtime-enforcement, hardware-capabilities, sanitizers]
sources: [raw/papers/cheri-llm-memory-safety.md, raw/articles/hn-fil-c.md, raw/articles/clang-bounds-safety.md]
confidence: medium
---

# Runtime memory-safety enforcement

Runtime enforcement traps violations when they happen instead of proving they cannot happen. The options in these sources are [[cheri]] in hardware, [[fil-c]] as a compiler and runtime, `-fbounds-safety` checks, and ASan/Valgrind ([[sanitizers-and-valgrind]]). On LLM code, CHERI caught everything the sanitizers caught, plus latent spatial bugs. ^[raw/papers/cheri-llm-memory-safety.md] ^[raw/articles/hn-fil-c.md] ^[raw/articles/clang-bounds-safety.md]

For the project, these work as test-time oracles and as a deployment backstop beneath proofs.

## Related

- [[cheri-llm-memory-safety]]
- [[hn-fil-c]]
- [[clang-bounds-safety]]
- [[safe-c-approaches-compared]]
