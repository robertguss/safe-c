---
name: research-wiki
description: Run an operation on the Safe C research wiki (ingest a source, answer a query, record a decision, lint). Use when Robert shares a paper, article or talk, asks what the research says, makes a design decision, or a session adds wiki pages.
---

`research-wiki/SCHEMA.md` is the authority on page types, frontmatter, tags, thresholds, and conventions. Read it, then `index.md`, then the last 20 entries of `log.md`, before touching the wiki. This skill is the order of operations; the schema is the rulebook.

Every operation ends the same way, and the wiki is not **settled** until all three hold:

- `index.md` lists every page, with its "Last updated" date and page count current;
- `log.md` has one new entry, `## [YYYY-MM-DD] <op> | <title>`, with bullets for what changed and a `Lint:` line;
- `python3 research-wiki/tools/lint.py` reports 0 issues, or each issue is named for Robert.

## Ingest

1. Save the source under `raw/` with its frontmatter (`source_url`, `ingested`, `sha256` of the body). If a file with the same URL exists, compare the hash: unchanged means stop; changed means flag it for Robert.
2. Read the whole source. Give Robert its three to five takeaways for Safe C, and what it contradicts in existing pages, then let him steer the emphasis before writing.
3. Write the `sources/` page. Then, per the schema's page thresholds, update or create the entity and concept pages it touches, each with two or more outbound wikilinks.
4. If the source changes the thesis, rewrite `synthesis.md` (rewrite, never append).
5. Settle the wiki.

## Query

1. Pick pages from `index.md`, read them, and answer with a citation to each page you draw on.
2. When the answer is worth keeping, file it under `queries/` or `comparisons/`, then settle the wiki.

## Record a decision

When Robert decides a design question (as with SC-05's epoch tags and its "restate + waive" alignment rule), add the decision, its date, its reason, and its evidence path to the concept page it belongs to, usually `concepts/acsl-patterns-arena-slice.md` or a sibling. Log it with op `update`, then settle the wiki.

## Lint

Run the lint, fix every mechanical issue (index gaps, broken links, frontmatter), and name contested or low-confidence pages for Robert. Log it with op `lint`.
