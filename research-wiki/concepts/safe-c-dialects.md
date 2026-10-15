---
title: "Safe C dialects and annotation schemes"
created: 2026-10-14
updated: 2026-10-14
type: concept
tags: [safe-dialect, bounds-annotations, annotation-migration]
sources: [raw/papers/msa-icse-2025.md, raw/articles/clang-bounds-safety.md, raw/articles/hn-fil-c.md]
confidence: medium
---

# Safe C dialects and annotation schemes

There are two families. Annotation dialects ([[checked-c]], [[clang-bounds-safety]]) need bounds information in the source and reject code that lacks it. Whole-implementation approaches ([[fil-c]]) compile ordinary C with a different runtime and ABI. ^[raw/papers/msa-icse-2025.md] ^[raw/articles/clang-bounds-safety.md] ^[raw/articles/hn-fil-c.md]

Annotation dialects stalled because the work was manual. MSA shows LLMs can do that work, which suggests annotation dialects are a better fit for agents than for humans. (This last step is our inference.) MISRA C was in the original scope, but no source here covers it. That is a gap.

## Related

- [[msa-icse-2025]]
- [[clang-bounds-safety]]
- [[hn-fil-c]]
- [[safe-c-approaches-compared]]
