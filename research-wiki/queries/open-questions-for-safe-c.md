---
title: "Open questions for Safe C for agents"
created: 2026-10-14
updated: 2026-10-18
type: query
tags: [agent-workflow, spec-synthesis, benchmark]
sources: [raw/papers/sun-2024-formai-v2.md, raw/papers/autoacsl-2026.md, raw/papers/cnnotator-2026.md, raw/papers/msa-icse-2025.md, raw/articles/hn-fil-c.md]
---

# Open questions for Safe C for agents

**Question:** what does the 2024–2026 literature leave open that the project could answer?

**Drawn on:** [[llm-generated-c-vulnerabilities]], [[specification-synthesis]], [[safe-c-dialects]], [[safe-c-approaches-compared]].

1. **Agentic, not zero-shot, measurement.** Every vulnerability rate here comes from zero-shot output. No study re-measures FormAI-style rates for an agent that must pass ESBMC, sanitizers and a WP proof before finishing. ^[raw/papers/sun-2024-formai-v2.md]
2. **Spec adequacy.** High full-proof rates (96%) do not show that the contract captured intent. The project needs a guard against weak specs, such as mutation testing of contracts or required postconditions. SC-14 adds `make mutate`, which requires WP to reject a fixed set of code mutations of every proved file; it guards the contract against the code it covers, not against a missing property. SC-27 adds the missing half: a model from another provider writes each module's required properties from the task brief, seeing no contract, and `make properties` proves each from the contract alone. Its first run found three handle-module guarantees stated in a form no caller could use. Still open: the backfilled briefs were written alongside their code, so they are weaker evidence than a brief written first. ^[raw/papers/autoacsl-2026.md]
3. **A subset designed for provers.** None of the sources defines a C subset chosen so that LLM-written ACSL or CN discharges easily. MISRA was not covered by any source ingested here.
4. **Annotation dialect as the agent's native tongue.** MSA and CNnotator show LLMs can carry the annotation burden that stalled Checked C. Whether `-fbounds-safety` plus ACSL beats either one alone is untested. ^[raw/papers/msa-icse-2025.md] ^[raw/papers/cnnotator-2026.md]
5. **Fil-C/CHERI as the test oracle.** Running agent tests under Fil-C to surface latent bugs that sanitizers miss looks cheap. No source evaluates it with agents. ^[raw/articles/hn-fil-c.md]

## Related

- [[synthesis]]
- [[safe-c-approaches-compared]]
- [[verifier-in-the-loop]]
- [[acsl-patterns-arena-slice]]
