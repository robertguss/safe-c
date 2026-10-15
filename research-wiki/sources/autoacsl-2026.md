---
title: "AutoACSL: ACSL synthesis with code-property-graph analysis"
created: 2026-10-14
updated: 2026-10-14
type: source
tags: [spec-synthesis, acsl, formal-verification, static-analysis]
sources: [raw/papers/autoacsl-2026.md]
---

# AutoACSL: ACSL synthesis with code-property-graph analysis

**Date:** 2026-06 (arXiv 2606.20969)  
**URL:** <https://arxiv.org/abs/2606.20969>  
**Raw:** `raw/papers/autoacsl-2026.md`

## What it claims

Zhou, Luo and Xu extract semantic features from a code property graph (arithmetic, loops and recursion, return-value flow) and put them into structured prompts. The model then writes both behavioral contracts and preconditions that rule out runtime errors. The candidates are refined against [[frama-c]]/WP. The authors report a 98% generation success rate and a 96% full-proof rate with Gemini-3, and a 24.7–51.7% improvement in full-proof ratio over a code-only prompt across four models.

## What it adds

Shows how much the prompt context matters. Static-analysis facts fed to the model nearly close the gap to full proof on the benchmarks.

## Related

- [[specification-synthesis]]
- [[autospec-2024]]
- [[frama-c]]
- [[verifier-in-the-loop]]
