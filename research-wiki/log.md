# Wiki Log

> Chronological record of all vault actions. Append-only. Format:
> `## [YYYY-MM-DD] action | subject` Actions: create, ingest, update, query,
> lint, archive, session Rotate to `log-YYYY.md` past 500 entries.

## [2026-10-14] create | Vault initialized

- Domain: LLMs/agents writing C safely, for Safe C for agents; preset: research vault
- Files: SCHEMA.md, index.md, log.md, tools/lint.py, raw/{articles,papers,transcripts,assets}, sources/, entities/, concepts/, comparisons/, queries/

## [2026-10-14] ingest | Batch of 14 sources (Exa contents API)

- raw/papers: sun-2024-formai-v2, llm-csec-2025, cheri-llm-memory-safety, autospec-2024, autoacsl-2026, cnnotator-2026, msa-icse-2025, crust-bench-2025, esbmc-ai-2023
- raw/articles: google-scaling-memory-safety, hn-fil-c, hn-llm-near-c-valgrind, darpa-tractor, clang-bounds-safety
- sources/: one page per raw file (14)
- entities/: esbmc, cbmc, frama-c, cn, checked-c, fil-c, cheri
- concepts/: llm-generated-c-vulnerabilities, verifier-in-the-loop, specification-synthesis, safe-c-dialects, runtime-memory-safety, sanitizers-and-valgrind, c-to-rust-translation
- comparisons/safe-c-approaches-compared; queries/open-questions-for-safe-c; synthesis.md; index.md
- Gap noted: no MISRA C source ingested

## [2026-10-14] lint | Clean

- 31 wiki pages checked, 0 issues (fixed earlier: synthesis orphan, comparison confidence set to medium with its verdict marked as a recommendation)
