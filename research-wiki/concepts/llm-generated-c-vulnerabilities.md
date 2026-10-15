---
title: "LLM-generated C vulnerabilities"
created: 2026-10-14
updated: 2026-10-14
type: concept
tags: [empirical-study, cwe]
sources: [raw/papers/sun-2024-formai-v2.md, raw/papers/llm-csec-2025.md, raw/papers/cheri-llm-memory-safety.md, raw/articles/hn-llm-near-c-valgrind.md]
confidence: high
---

# LLM-generated C vulnerabilities

Unconstrained LLMs write vulnerable C by default. FormAI-v2 found at least 62.07% of 331K programs vulnerable under [[esbmc]], with little difference between models. ^[raw/papers/sun-2024-formai-v2.md]

A SAST study of ten models reached the same conclusion, mapped to CWEs and CVEs. ^[raw/papers/llm-csec-2025.md]

On CHERI, the violations were in pointer and raw-buffer handling and looked like novice errors. ^[raw/papers/cheri-llm-memory-safety.md]

Practitioners on HN were split between 'never' and 'make the agent run valgrind'. ^[raw/articles/hn-llm-near-c-valgrind.md]

**Open:** none of these studies tests an agent that runs a checker in the loop. They all measure zero-shot output. See [[verifier-in-the-loop]].

## Related

- [[sun-2024-formai-v2]]
- [[llm-csec-2025]]
- [[cheri-llm-memory-safety]]
- [[safe-c-approaches-compared]]
