---
title: "HN: Fil-C, a memory-safe C implementation"
created: 2026-10-14
updated: 2026-10-14
type: source
tags: [runtime-enforcement, community, safe-dialect]
sources: [raw/articles/hn-fil-c.md]
confidence: medium
---

# HN: Fil-C, a memory-safe C implementation

**Date:** 2025 (HN item 45735877, about LWN article)  
**URL:** <https://news.ycombinator.com/item?id=45735877>  
**Raw:** `raw/articles/hn-fil-c.md`

## What it claims

A Hacker News thread (283 points) on an LWN piece about [[fil-c]]. In the thread, people report packaging Fil-C as a Nix toolchain that builds tmux, coreutils, Perl, Lua, SQLite and more. Commenters discuss the ABI break, the performance cost, and where it fits, such as enterprise distros and isolated packages.

## What it adds

Community signal that whole-program memory-safe C is usable today if you accept the overhead. For agents, it could serve as a strict test or CI target. (This last point is our recommendation, not a claim from the thread.)

## Related

- [[fil-c]]
- [[runtime-memory-safety]]
- [[safe-c-dialects]]
