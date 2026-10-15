---
title: "Safe C approaches for agents, compared"
created: 2026-10-14
updated: 2026-10-14
type: comparison
tags: [safe-dialect, formal-verification, c-to-rust, runtime-enforcement]
sources: [raw/papers/sun-2024-formai-v2.md, raw/papers/autoacsl-2026.md, raw/papers/msa-icse-2025.md, raw/papers/cheri-llm-memory-safety.md, raw/papers/crust-bench-2025.md, raw/articles/google-scaling-memory-safety.md]
confidence: medium
---

# Safe C approaches for agents, compared

| Approach | Guarantee | Agent fit | Evidence |
| --- | --- | --- | --- |
| Zero-shot C | none | n/a | ≥62% vulnerable ^[raw/papers/sun-2024-formai-v2.md] |
| Sanitizers / Valgrind | dynamic, partial | trivial to loop | misses latent bugs ^[raw/papers/cheri-llm-memory-safety.md] |
| BMC repair (ESBMC/CBMC) | bounded proof | strong: counterexamples | [[esbmc-ai-2023]] |
| ACSL + Frama-C/WP | deductive proof | strong: 79–96% on benchmarks | ^[raw/papers/autoacsl-2026.md] |
| Annotation dialect (Checked C, -fbounds-safety) | compile-time + trap | good: LLM porting works | ^[raw/papers/msa-icse-2025.md] |
| Runtime (Fil-C, CHERI) | trap, whole program | zero annotation | [[cheri-llm-memory-safety]] |
| Rewrite to Rust | type system | hard at scale | 15/100 single-shot ^[raw/papers/crust-bench-2025.md] |

**Verdict (agent recommendation, not settled):** a layered stack fits the project thesis: a strict subset with bounds annotations, ACSL contracts proved by WP where it matters, BMC and sanitizers in the loop, and Fil-C or CHERI as a test-time oracle. Rust translation suits small, heavily tested libraries ([[google-scaling-memory-safety]]). DARPA's 'bug-finding is not enough' ([[darpa-tractor]]) is aimed at the sanitizer rows, not the proof rows.

## Related

- [[synthesis]]
- [[c-to-rust-translation]]
- [[safe-c-dialects]]
- [[verifier-in-the-loop]]
- [[runtime-memory-safety]]
- [[specification-synthesis]]
