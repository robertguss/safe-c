---
title: "Google: AI-assisted rewrites of C/C++ dependencies to Rust"
created: 2026-10-14
updated: 2026-10-14
type: source
tags: [c-to-rust, industry, differential-testing]
sources: [raw/articles/google-scaling-memory-safety.md]
---

# Google: AI-assisted rewrites of C/C++ dependencies to Rust

**Date:** 2026-08-24 (Google Bug Hunters blog)  
**URL:** <https://bughunters.google.com/blog/scaling-memory-safety>  
**Raw:** `raw/articles/google-scaling-memory-safety.md`

## What it claims

Kersting and Hils describe rewriting giflib (about 3K LOC) into an ABI-compatible Rust drop-in with Gemini. The steps were a one-shot translation, iteration on the FFI layer (the first version had very unsound memory management there), and a loop that fed differential-test failures back to the model. Validation covered more than 30M GIFs, over 200M differential-fuzzing iterations, and adversarial LLM review. The work found a pre-existing out-of-bounds write, and the rewrite was already deployed when CVE-2026-26740 hit the C original.

## What it adds

Production evidence that the expensive part of AI rewrites is validation and the trust of service owners, not translation.

## Related

- [[c-to-rust-translation]]
- [[crust-bench-2025]]
- [[darpa-tractor]]
- [[verifier-in-the-loop]]
