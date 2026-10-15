---
title: "Clang -fbounds-safety design document"
created: 2026-10-14
updated: 2026-10-14
type: source
tags: [safe-dialect, bounds-annotations, runtime-enforcement]
sources: [raw/articles/clang-bounds-safety.md]
---

# Clang -fbounds-safety design document

**Date:** undated design doc (accessed 2026-10-14)  
**URL:** <https://clang.llvm.org/docs/BoundsSafety.html>  
**Raw:** `raw/articles/clang-bounds-safety.md`

## What it claims

A C extension in which annotations such as `__counted_by(N)` attach bounds to pointers. The compiler inserts checks that trap deterministically on out-of-bounds access, and it rejects code that lacks enough bounds information. The doc notes the feature is not yet available to users and that it is designed to keep the annotation burden low.

## What it adds

The upstream-compiler counterpart of [[checked-c]]. Its annotations are a natural target for agents to write.

## Related

- [[safe-c-dialects]]
- [[checked-c]]
- [[msa-icse-2025]]
