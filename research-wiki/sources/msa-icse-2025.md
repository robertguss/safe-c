---
title: "LLM Assistance for Memory Safety (MSA)"
created: 2026-10-14
updated: 2026-10-14
type: source
tags: [safe-dialect, annotation-migration, static-analysis]
sources: [raw/papers/msa-icse-2025.md]
---

# LLM Assistance for Memory Safety (MSA)

**Date:** 2025-05-01 (ICSE 2025 Research Track)  
**URL:** <https://conf.researchr.org/details/icse-2025/icse-2025-research-track/25/LLM-Assistance-for-Memory-Safety>  
**Raw:** `raw/papers/msa-icse-2025.md`

## What it claims

Mohammed, Lal, Rastogi, Roy and Sharma (Microsoft Research and IIT Kanpur) built MSA, which ports C to [[checked-c]]. LLMs infer the annotations and refactor code where needed, and lightweight static analysis breaks the whole-program transformation into small steps. On codebases of up to 20K LOC it beat a plain LLM baseline and a state-of-the-art symbolic tool.

## What it adds

The main reason annotation-based safe dialects saw little adoption was the manual porting work. LLMs can take that work on.

## Related

- [[safe-c-dialects]]
- [[checked-c]]
- [[cnnotator-2026]]
- [[clang-bounds-safety]]
