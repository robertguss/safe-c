---
title: "LLM-assisted C-to-Rust translation"
created: 2026-10-14
updated: 2026-10-14
type: concept
tags: [c-to-rust, benchmark, industry]
sources: [raw/articles/darpa-tractor.md, raw/papers/crust-bench-2025.md, raw/articles/google-scaling-memory-safety.md]
confidence: medium
---

# LLM-assisted C-to-Rust translation

The competing route. DARPA argues bug-finding is not enough and is funding automatic translation. ^[raw/articles/darpa-tractor.md]

On CRUST-Bench, the best model solved only 15 of 100 repos single-shot. ^[raw/papers/crust-bench-2025.md]

Google's giflib rewrite worked, but it needed FFI unsafe code reviewed by experts and very large differential validation. ^[raw/articles/google-scaling-memory-safety.md]

Today, translation looks practical for small, well-tested libraries and hard at repository scale. See [[safe-c-approaches-compared]].

## Related

- [[darpa-tractor]]
- [[crust-bench-2025]]
- [[google-scaling-memory-safety]]
- [[safe-c-approaches-compared]]
