---
title: "ESBMC-AI: self-healing software via LLMs and formal verification"
created: 2026-10-14
updated: 2026-10-14
type: source
tags: [model-checking, repair-loop, formal-verification]
sources: [raw/papers/esbmc-ai-2023.md]
---

# ESBMC-AI: self-healing software via LLMs and formal verification

**Date:** 2023-05 (arXiv 2305.14752, v2 2024-06)  
**URL:** <https://arxiv.org/abs/2305.14752>  
**Raw:** `raw/papers/esbmc-ai-2023.md`

## What it claims

Tihanyi et al. run [[esbmc]] to find a vulnerability and get a counterexample, give the code, stack trace and counterexample to an LLM, and re-verify the fix with BMC. They evaluated the method on 50,000 FormAI programs, covering buffer overflows, arithmetic overflows and pointer dereference failures, and propose it as a CI/CD step.

## What it adds

The earliest clear verifier-feedback repair loop for C. Predates the 2024–2026 window but anchors it.

## Related

- [[verifier-in-the-loop]]
- [[esbmc]]
- [[sun-2024-formai-v2]]
