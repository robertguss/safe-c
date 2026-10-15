---
title: "How secure is AI-generated code (FormAI-v2)"
created: 2026-10-14
updated: 2026-10-14
type: source
tags: [empirical-study, cwe, model-checking]
sources: [raw/papers/sun-2024-formai-v2.md]
---

# How secure is AI-generated code (FormAI-v2)

**Date:** 2024-04 (arXiv 2404.18353; Empirical Software Engineering)  
**URL:** <https://arxiv.org/abs/2404.18353>  
**Raw:** `raw/papers/sun-2024-formai-v2.md`

## What it claims

Tihanyi et al. had nine LLMs (GPT-4o-mini, Gemini Pro 1.0, Falcon-180B, Code Llama 13B and smaller models) write C programs from a neutral zero-shot prompt and built FormAI-v2: 331,000 compilable C programs, each labelled by [[esbmc]] bounded model checking. At least 62.07% of the programs were vulnerable. The models differed little and made similar errors. The first FormAI dataset (112,000 GPT-3.5 programs) had a 51.24% vulnerable rate.

## What it adds

The baseline number for the project: when nothing constrains an LLM, most of the C it writes fails a model checker. Labels come with counterexamples, so false positives are low.

## Related

- [[llm-generated-c-vulnerabilities]]
- [[esbmc]]
- [[esbmc-ai-2023]]
- [[llm-csec-2025]]
