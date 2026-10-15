---
title: "Sanitizers and Valgrind"
created: 2026-10-14
updated: 2026-10-14
type: concept
tags: [sanitizers, agent-workflow]
sources: [raw/articles/hn-llm-near-c-valgrind.md, raw/papers/cheri-llm-memory-safety.md]
confidence: medium
---

# Sanitizers and Valgrind

The cheap dynamic baseline. HN's minimum bar was 'at least pass valgrind' ([[hn-llm-near-c-valgrind]]). The CHERI study shows ASan and Valgrind miss latent spatial violations that capability hardware traps ([[cheri-llm-memory-safety]]). They are necessary but not sufficient.

## Related

- [[hn-llm-near-c-valgrind]]
- [[cheri-llm-memory-safety]]
- [[safe-c-approaches-compared]]
