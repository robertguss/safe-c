---
title: "Memory safety of LLM-generated code on CHERI"
created: 2026-10-14
updated: 2026-10-14
type: source
tags: [empirical-study, runtime-enforcement, hardware-capabilities]
sources: [raw/papers/cheri-llm-memory-safety.md]
---

# Memory safety of LLM-generated code on CHERI

**Date:** 2026-05 (CSCWD 2026, Glasgow eprint 381094)  
**URL:** <https://eprints.gla.ac.uk/381094/>  
**Raw:** `raw/papers/cheri-llm-memory-safety.md`

## What it claims

Qin, Singer and Wang had local LLMs write 120 compilable programs for binary parsing, record decoding and image/log processing. They checked the programs with [[cbmc]], ASan, Valgrind and [[cheri]] (Arm Morello). Memory-safety violations were common and looked like novice mistakes in pointer and raw-buffer code. CHERI caught every violation the other tools found, and it also trapped spatial violations that stay latent on ordinary hardware. A prompt-plus-RAG prototype combined with CHERI and WARDuino reported a 3.2× speedup and an 86% first-run success rate.

## What it adds

Evidence that a capability runtime can act as a backstop for agent-written C, beyond what sanitizers catch.

## Related

- [[llm-generated-c-vulnerabilities]]
- [[runtime-memory-safety]]
- [[cheri]]
- [[cbmc]]
