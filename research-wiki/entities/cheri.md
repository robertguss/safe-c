---
title: "CHERI"
created: 2026-10-14
updated: 2026-10-14
type: entity
tags: [hardware-capabilities, runtime-enforcement]
sources: [raw/papers/cheri-llm-memory-safety.md]
---

# CHERI

**Kind:** system

A hardware capability architecture (prototyped on Arm Morello) that enforces pointer bounds and provenance. In [[cheri-llm-memory-safety]] it trapped every violation found in LLM-written code, including latent spatial ones. It is a hardware counterpart to [[fil-c]].

## Related

- [[cheri-llm-memory-safety]]
- [[runtime-memory-safety]]
- [[fil-c]]
