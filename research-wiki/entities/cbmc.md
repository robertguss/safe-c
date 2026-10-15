---
title: "CBMC"
created: 2026-10-14
updated: 2026-10-15
type: entity
tags: [model-checking, formal-verification]
sources: [raw/papers/cheri-llm-memory-safety.md]
---

# CBMC

**Kind:** system

A bounded model checker for C. [[cheri-llm-memory-safety]] uses it next to ASan and Valgrind as a static check on LLM-written programs. It has the same role as [[esbmc]]: exhaustive up to a bound, with counterexamples the agent can act on.

## Related

- [[cheri-llm-memory-safety]]
- [[verifier-in-the-loop]]
- [[esbmc]]
- [[acsl-patterns-arena-slice]]
