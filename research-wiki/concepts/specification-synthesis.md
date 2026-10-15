---
title: "LLM specification synthesis (ACSL, CN)"
created: 2026-10-14
updated: 2026-10-14
type: concept
tags: [spec-synthesis, acsl, separation-logic]
sources: [raw/papers/autospec-2024.md, raw/papers/autoacsl-2026.md, raw/papers/cnnotator-2026.md]
confidence: medium
---

# LLM specification synthesis (ACSL, CN)

LLMs now write contracts that provers accept at high rates on benchmarks. AutoSpec reached 79% full proof. ^[raw/papers/autospec-2024.md]

AutoACSL reached 96% with Gemini-3 when given code-property-graph facts. ^[raw/papers/autoacsl-2026.md]

CNnotator's o3 run reached 97% on [[cn]] memory specs. ^[raw/papers/cnnotator-2026.md]

All three validate every candidate against a checker ([[frama-c]] or CN). None treats a spec that verifies as one that is correct. Spec adequacy (does the contract say what was intended?) is still on the human.

## Related

- [[autospec-2024]]
- [[autoacsl-2026]]
- [[cnnotator-2026]]
- [[safe-c-approaches-compared]]
