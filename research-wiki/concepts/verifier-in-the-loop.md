---
title: "Verifier-in-the-loop (guess and check)"
created: 2026-10-14
updated: 2026-10-22
type: concept
tags: [repair-loop, formal-verification, agent-workflow]
sources: [raw/papers/esbmc-ai-2023.md, raw/papers/autospec-2024.md, raw/papers/cnnotator-2026.md, raw/articles/google-scaling-memory-safety.md, raw/articles/hn-llm-near-c-valgrind.md]
confidence: high
---

# Verifier-in-the-loop (guess and check)

The LLM proposes code, specs or fixes, and a trusted checker accepts or rejects them, returning a counterexample or failure as feedback. Because the checker is trusted, the guesser does not need to be. ^[raw/papers/cnnotator-2026.md]

The checker varies: BMC counterexamples ([[esbmc-ai-2023]]), WP proof obligations ([[autospec-2024]]), CN tests, differential fuzzing at Google scale ([[google-scaling-memory-safety]]), or simply valgrind ([[hn-llm-near-c-valgrind]]). ^[raw/papers/esbmc-ai-2023.md] ^[raw/articles/google-scaling-memory-safety.md]

**Open:** which checker gives the most useful feedback per token, and how to stop a loop from 'fixing' a failure by weakening the spec. Neither question is measured in these sources.

## Decision: checks run by hand (2026-10-22)

Robert decided that Safe C's checks run only when someone asks for them. A Gates run on GitHub took 30 to 110 minutes, most were cancelled by the next push before they finished, and agents were told they were done only when the full `make ci` list was green. That cost outweighed what the checks caught during day-to-day development. Now nothing runs on push, pull request or a schedule. `make check` (lint plus sanitizer tests, about 30 seconds) is the default before calling a change done, the table in `AGENTS.md` names the heavier check for each kind of change, and the GitHub workflows run only when started by hand. Every check is still in the tree; none was weakened. Revisit if regressions start reaching `main` unnoticed. Evidence: `docs/plans/2026-10-22-2232-chore-manual-only-checks-plan.md`.

## Related

- [[esbmc-ai-2023]]
- [[autospec-2024]]
- [[cnnotator-2026]]
- [[safe-c-approaches-compared]]
