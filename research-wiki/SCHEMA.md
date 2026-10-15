# Safe C for agents — Research Wiki — Schema

Read this first, every session. Then `index.md`, then the last 20 entries of
`log.md`. Only then touch anything.

## Domain

This wiki covers research and public discussion (mainly 2024–2026) on LLMs and AI agents writing C: how unsafe their C is, LLM plus verifier loops (ESBMC, CBMC, Frama-C/WP, CN), LLM spec synthesis, safe C dialects and runtimes (Checked C, -fbounds-safety, Fil-C, CHERI, MISRA), and C-to-Rust translation as the contrast. It serves Robert Guss's Safe C for agents project (see `docs/project-sketch.md`). Robert curates sources and asks the questions. The agent summarizes, cross-references, files, and keeps pages consistent. Out of scope: general Rust advocacy, non-C languages, and secrets or credentials of any kind.

The wiki lives in `research-wiki/`, the Obsidian vault root.

## Layout

```
research-wiki/
  SCHEMA.md          this file
  index.md           every wiki page, one line each, by section
  log.md             append-only action log
  synthesis.md       the evolving thesis, rewritten as sources land
  raw/               immutable sources
    articles/        web clippings
    papers/          PDFs and their text
    code/            source files and proof harnesses from repos
    transcripts/     talks, interviews, meetings
    assets/          images the sources reference
  sources/           one summary page per ingested source
  entities/          people, organizations, products, systems
  concepts/          topics and ideas
  comparisons/       side-by-side analyses
  queries/           answers worth keeping
  tools/             lint.py and other vault tooling
```

## Page types

| type         | lives in      | what it is                                                             |
| ------------ | ------------- | ---------------------------------------------------------------------- |
| `source`     | sources/      | one ingested source: what it claims, what it adds, what it contradicts |
| `entity`     | entities/     | one notable person, organization, product, or system                   |
| `concept`    | concepts/     | one idea: definition, current state of knowledge, open questions       |
| `comparison` | comparisons/  | things held side by side, a table and a verdict                        |
| `query`      | queries/      | a filed answer: the question, the pages drawn on, the answer           |
| `synthesis`  | the wiki root | the evolving thesis across everything read                             |

## Frontmatter (required on every wiki page)

```yaml
---
title: "Transformer architecture"
created: 2026-09-16
updated: 2026-09-16
type: concept # from the table above
tags: [architecture] # from the taxonomy below only
sources: [raw/papers/vaswani-2017-attention.md]
# optional quality signals:
confidence: high | medium | low
contested: true
contradictions: [other-page-slug]
---
```

Every page starts with `# Title` matching the frontmatter title, then body, then
a `## Related` list. Minimum two outbound `[[wikilinks]]` per page.

Raw files carry their own small frontmatter: `source_url`, `ingested`, and
`sha256` of the body below the frontmatter, so a re-ingest can skip an unchanged
source and flag a changed one.

## Tag taxonomy

Add a tag here before using it. Keep it under 25 (23 now).

- Evidence: `empirical-study`, `benchmark`, `cwe`, `community`, `industry`, `government`
- Verification: `formal-verification`, `model-checking`, `static-analysis`, `spec-synthesis`, `acsl`, `separation-logic`, `repair-loop`, `differential-testing`
- Safety mechanisms: `safe-dialect`, `bounds-annotations`, `annotation-migration`, `runtime-enforcement`, `hardware-capabilities`, `sanitizers`
- Alternatives and workflow: `c-to-rust`, `agent-workflow`, `allocators`

## Page thresholds

- A page when an entity or concept appears in two or more sources, or is central
  to one.
- An update to the existing page when a source mentions something already
  covered.
- Nothing for a passing mention or a thing outside the domain.
- A split when a page passes about 200 lines, into sub-topics that link.
- An archive (`_archive/`, out of the index, inbound links made plain text) when
  a page is fully superseded.

## Conventions

- File names: lowercase, hyphens.
- Newer sources generally supersede older ones; a genuine contradiction keeps
  both positions with dates and sources, marks `contested: true` and
  `contradictions:` on both pages, and is named for the human at the next lint.
- Provenance: on a page drawing on three or more sources, `^[raw/...]` at the
  end of each paragraph whose claim comes from one of them.
- `confidence:` set to `medium` or `low` on single-source, opinion-heavy, or
  fast-moving claims; `high` only when several sources agree.
- `updated:` is bumped on every edit. `created:` never changes.
- `raw/` is immutable. Corrections go on wiki pages.

## Operations

- **Ingest** one source at a time by default, discussing takeaways with the
  human before writing, so they steer what gets emphasized. A batch ingest reads
  every source first, checks existing pages once, and writes one log entry.
- **Query** reads `index.md` to pick pages, answers with citations, and files an
  answer worth keeping under `queries/` or `comparisons/`.
- **Lint** at the end of any session that added pages; the count goes in
  `log.md`.
- **The synthesis** is rewritten, not appended, whenever a source changes the
  picture.

## Lint

`python3 research-wiki/tools/lint.py` from the repo root: broken wikilinks, orphans,
index completeness, required frontmatter, tags in taxonomy, raw sha256 drift,
contested or low-confidence pages, pages over 200 lines, log size.
