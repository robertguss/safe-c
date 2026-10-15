---
title: "Synthesis: can agents write near-provably safe C?"
created: 2026-10-14
updated: 2026-10-14
type: synthesis
tags: [agent-workflow, formal-verification]
sources: [raw/papers/sun-2024-formai-v2.md, raw/papers/cnnotator-2026.md, raw/papers/autoacsl-2026.md, raw/papers/msa-icse-2025.md, raw/papers/crust-bench-2025.md, raw/articles/darpa-tractor.md]
confidence: medium
---

# Synthesis: can agents write near-provably safe C?

**In one paragraph.** Left alone, LLMs write vulnerable C at rates above 50% ([[llm-generated-c-vulnerabilities]]). The same models do well at the tedious half of safe C: writing ACSL contracts and CN memory specs, and porting to annotated dialects, at 79–97% success against trusted checkers ([[specification-synthesis]]). This supports the project thesis that agents can absorb the tedium that kept strict C rare, as long as a checker always has the final say ([[verifier-in-the-loop]]). ^[raw/papers/sun-2024-formai-v2.md] ^[raw/papers/cnnotator-2026.md] ^[raw/papers/autoacsl-2026.md]

**The competing route.** DARPA and Google are moving C to Rust. At repository scale this is still hard, and in production it is validation-bound ([[c-to-rust-translation]]). The best bet is a layered stack, not a single tool ([[safe-c-approaches-compared]]). ^[raw/articles/darpa-tractor.md] ^[raw/papers/crust-bench-2025.md]

**What would change this picture:** an agentic benchmark showing proof-gated agents still ship bugs, or translation benchmarks jumping well past 15/100. See [[open-questions-for-safe-c]].

## Related

- [[safe-c-approaches-compared]]
- [[open-questions-for-safe-c]]
- [[llm-generated-c-vulnerabilities]]
