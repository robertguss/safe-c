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

## [2026-10-15] ingest | ACSL/CBMC patterns for milestone 1 (arena + slices)

- raw/code: acsl-by-example-{copy,fill,find}, frama-c-libc-{string,stdlib}-h, aws-c-common-{byte-buf-c,byte-buf-append-harness,math-fallback}, s2n-tls-stuffer-c
- raw/papers: mangano-2016-contiki-memb, frama-c-rte-manual; raw/articles: cbmc-memory-predicates, cbmc-loop-contracts, so-unsigned-wraparound-frama-c
- New: concepts/acsl-patterns-arena-slice. Updated: entities/frama-c, entities/cbmc, queries/open-questions-for-safe-c, index.md, SCHEMA.md (raw/code/, tag `allocators`)
- Gaps: no proved bump arena found; no \block_length or ghost-model arena in fetched sources; reset-invalidation pattern unsourced (recommendation only)

## [2026-10-15] lint | Clean

- 32 wiki pages checked, 0 issues

## [2026-10-15] update | SC-05 epoch decision

- Extended the ghost epoch experiment to two arenas; a preserved stale token is rejected, but recapturing the token after reset passes. Robert selected runtime epoch tags.
- Updated concepts/acsl-patterns-arena-slice with the decision, caller obligations, and remaining Frama-C model gaps. Evidence lives in docs/evidence/sc05/.
- Lint: 32 wiki pages checked, 0 issues.

## [2026-10-16] update | SC-05 alignment restatement and RTE guard waivers

- Recorded Robert's "restate + waive" decision in concepts/acsl-patterns-arena-slice: `\aligned` is unsupported in Frama-C 33, so arena contracts restate 8-byte alignment as integer offset arithmetic (`used % 8 == 0` and `(\result.ptr - a->base) % 8 == 0`) with caller base alignment as a documented precondition, and `waivers.toml` waives the four skipped `<translation-unit>` RTE guards (`wp-rte-unaligned` and `wp-rte-function-pointer` on `stdlib/arena.c` and `stdlib/slice.c`).
- Lint: 32 wiki pages checked, 0 issues.

## [2026-10-17] update | SC-06 string design note

- Added the `sc_str` design (typedef of `sc_slice`, copying `from_lit`, `str_match_at` predicate) to concepts/acsl-patterns-arena-slice. Marked unproved: Frama-C was not installable in the authoring session.
- Lint: 32 wiki pages checked, 0 issues.

## [2026-10-17] update | SC-07 tagged result and option

- Added a section to concepts/acsl-patterns-arena-slice: why the generic macros carry no ACSL, how the tag `requires` makes an unchecked unwrap a proof failure, and what lint rule R13 can and cannot see.
- Lint: 32 wiki pages checked, 0 issues.

## [2026-10-17] update | SC-09 generation-checked handles

- Added a section to concepts/acsl-patterns-arena-slice: byte-encoded slots for WP's typed model, linear carry on free, retirement at generation 65535, handles that carry epoch and table offset, slot bytes kept out of the slice API, and the option-returning get. CI proves `stdlib/handle.c` 245/245.
- Lint: 32 wiki pages checked, 0 issues.

## [2026-10-17] update | SC-10 ownership-passing channels

- Added a section to concepts/acsl-patterns-arena-slice: why the channel sits outside WP, per-thread handle tables, the ring and its memory ordering, and the no-epoch caveat.
- Lint: 32 wiki pages checked, 0 issues.

## [2026-10-17] update | SC-10 decisions

- Robert agreed to keep the proof skip of stdlib/chan.c in scripts/prove.py with no waiver, and to leave out an arena-epoch check on the channel; recorded in concepts/acsl-patterns-arena-slice.
- Lint: 32 wiki pages checked, 0 issues.

## [2026-10-18] update | SC-11 HTTP demo

- Added a section to concepts/acsl-patterns-arena-slice: the demo is a usage test, what the subset made awkward (constants copied per request, no header table, lint never compiled an application file), and how the adapter owns the arena lifecycle.
- Lint: 32 wiki pages checked, 0 issues.

## [2026-10-18] update | SC-14 mutation checks

- Open question 2 in queries/open-questions-for-safe-c now names `make mutate` as the answer to spec adequacy for code mutations, and its limit (it cannot find a property the contract never states).

## [2026-10-18] update | SC-13 literal compare and R2

- Added a section to concepts/acsl-patterns-arena-slice: `sc_str_eq_lit` replaces the per-request literal copies, and R2 now needs a bare identifier operand because Semgrep leaked a pointer-typed variable's type onto a same-named field (`s.len`). Known gap: arithmetic on a pointer field is not flagged.

## [2026-10-18] update | SC-17 expiring waivers

- Checked Frama-C 32.0, 32.1, 33.0 and the git changelog: none supports `\aligned` or `\valid_function` in WP. Recorded in entities/frama-c; the ten guard waivers were renewed to 2027-07-14.

## [2026-10-20] update | SC-16 adopter guide

- Added a section to concepts/acsl-patterns-arena-slice: every code example in the guide and agent prompt is checked (`make guide-test`), contract examples are verbatim header excerpts, and the guide states that application C is not proved here.
- Lint: 32 wiki pages checked, 0 issues.

## [2026-10-21] update | SC-19 mailbox channel

- Added a section to concepts/acsl-patterns-arena-slice: the many-to-one mailbox, its claim/publish protocol, receiver-side close, and the capacity-1 bug the fuzz harness found.
