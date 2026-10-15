---
title: "Verifier-in-the-loop (guess and check)"
created: 2026-10-14
updated: 2026-10-14
type: concept
tags: [repair-loop, formal-verification, agent-workflow]
sources: [raw/papers/esbmc-ai-2023.md, raw/papers/autospec-2024.md, raw/papers/cnnotator-2026.md, raw/articles/google-scaling-memory-safety.md, raw/articles/hn-llm-near-c-valgrind.md]
confidence: high
---

# Verifier-in-the-loop (guess and check)

The LLM proposes code, specs or fixes, and a trusted checker accepts or rejects them, returning a counterexample or failure as feedback. Because the checker is trusted, the guesser does not need to be. ^[raw/papers/cnnotator-2026.md]

The checker varies: BMC counterexamples ([[esbmc-ai-2023]]), WP proof obligations ([[autospec-2024]]), CN tests, differential fuzzing at Google scale ([[google-scaling-memory-safety]]), or simply valgrind ([[hn-llm-near-c-valgrind]]). ^[raw/papers/esbmc-ai-2023.md] ^[raw/articles/google-scaling-memory-safety.md]

**Open:** which checker gives the most useful feedback per token, and how to stop a loop from 'fixing' a failure by weakening the spec. Neither question is measured in these sources.

## Related

- [[esbmc-ai-2023]]
- [[autospec-2024]]
- [[cnnotator-2026]]
- [[safe-c-approaches-compared]]
