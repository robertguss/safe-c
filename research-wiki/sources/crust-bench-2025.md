---
title: "CRUST-Bench: C-to-safe-Rust transpilation benchmark"
created: 2026-10-14
updated: 2026-10-14
type: source
tags: [c-to-rust, benchmark]
sources: [raw/papers/crust-bench-2025.md]
---

# CRUST-Bench: C-to-safe-Rust transpilation benchmark

**Date:** 2025-04 (arXiv 2504.15254, v3 2025-10)  
**URL:** <https://arxiv.org/abs/2504.15254>  
**Raw:** `raw/papers/crust-bench-2025.md`

## What it claims

Khatry et al.: 100 C repositories, each paired with a hand-written safe-Rust interface and tests. The best single-shot model, OpenAI o1, solved only 15 tasks. Producing safe, idiomatic Rust at repository scale is still hard.

## What it adds

The contrast case: getting from C to safe Rust at repository scale is far from solved, which leaves room for a 'make the C itself safe' path.

## Related

- [[c-to-rust-translation]]
- [[google-scaling-memory-safety]]
- [[darpa-tractor]]
