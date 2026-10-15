---
title: "HN: 'I would never ever let an LLM anywhere near C code' (valgrind thread)"
created: 2026-10-14
updated: 2026-10-14
type: source
tags: [community, agent-workflow, sanitizers]
sources: [raw/articles/hn-llm-near-c-valgrind.md]
confidence: medium
---

# HN: 'I would never ever let an LLM anywhere near C code' (valgrind thread)

**Date:** 2025-08-15  
**URL:** <https://news.ycombinator.com/item?id=44915397>  
**Raw:** `raw/articles/hn-llm-near-c-valgrind.md`

## What it claims

Posted under 'I let LLMs write an Elixir NIF in C'. drumnerd writes that code from an LLM 'needs at least to pass valgrind'. simonw answers that you can tell Claude Code or Codex CLI to run valgrind as part of its iteration. Replies cover sandboxing an agent that has shell access, and rewriting the NIF in Rust with Rustler instead.

## What it adds

The skeptic's position and the agent-practitioner's answer in two posts. It is also an informal version of the [[verifier-in-the-loop]] idea.

## Related

- [[verifier-in-the-loop]]
- [[sanitizers-and-valgrind]]
- [[llm-generated-c-vulnerabilities]]
