---
title: "AutoSpec: LLM specification synthesis with static analysis and verification"
created: 2026-10-14
updated: 2026-10-14
type: source
tags: [spec-synthesis, acsl, formal-verification]
sources: [raw/papers/autospec-2024.md]
---

# AutoSpec: LLM specification synthesis with static analysis and verification

**Date:** 2024-04 (arXiv 2404.00762; CAV 2024)  
**URL:** <https://arxiv.org/abs/2404.00762>  
**Raw:** `raw/papers/autospec-2024.md`

## What it claims

Wen et al. use the LLM to generate candidate specifications. Static analysis decomposes the program so the model attends to one piece at a time, and each candidate is checked by the verifier every round so errors do not accumulate. With [[frama-c]] they fully verified 79% of the benchmark programs, 1.592× better than prior work, and applied the method to a real X509 parser.

## What it adds

The reference design for the 'LLM proposes, prover disposes' loop for ACSL.

## Related

- [[specification-synthesis]]
- [[frama-c]]
- [[autoacsl-2026]]
- [[verifier-in-the-loop]]
