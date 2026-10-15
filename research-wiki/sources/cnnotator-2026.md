---
title: "CNnotator: LLM-guided memory-safety annotation synthesis"
created: 2026-10-14
updated: 2026-10-14
type: source
tags: [spec-synthesis, formal-verification, separation-logic]
sources: [raw/papers/cnnotator-2026.md]
---

# CNnotator: LLM-guided memory-safety annotation synthesis

**Date:** 2026-04 (ReCode '26; arXiv 2606.21822)  
**URL:** <https://arxiv.org/abs/2606.21822>  
**Raw:** `raw/papers/cnnotator-2026.md`

## What it claims

Byrnes (CMU, Galois intern) and Dodds (Galois) have LLMs write [[cn]] separation-logic specifications that describe how C code uses memory, and then test them. OpenAI o3 succeeded on 90% of first attempts and 97% overall on small-to-medium programs. GPT-4o succeeded on 65% of first attempts. The paper describes the method as a guess-and-check loop: a trusted checker means the guesser does not need to be trusted.

## What it adds

The clearest statement of the trust argument the project depends on, applied to memory-ownership annotations.

## Related

- [[specification-synthesis]]
- [[cn]]
- [[verifier-in-the-loop]]
- [[msa-icse-2025]]
