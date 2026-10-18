---
title: "Frama-C / WP"
created: 2026-10-14
updated: 2026-10-18
type: entity
tags: [formal-verification, acsl]
sources: [raw/papers/autospec-2024.md, raw/papers/autoacsl-2026.md]
---

# Frama-C / WP

**Kind:** system

An analysis platform for C. Its WP plugin proves ACSL contracts deductively. It is the prover that both [[autospec-2024]] and [[autoacsl-2026]] loop against. It is the most likely 'near-provable' backend for the project.

## WP alignment and function-pointer support (checked 18 Oct 2026)

- Frama-C 33.0 Arsenic (24 Jul 2026) is the newest release. Its WP section lists "Warnings for unsupported RTEs" and nothing on `\aligned` or `\valid_function`; the `Skipped RTE guards` warning still appears.
- 32.0 Germanium added `\aligned` to the kernel, Eva, RTE and E-ACSL, but not WP. Allan Blanchard's alignment post (2 Mar 2026) says "The WP plug-in does not support alignment verification" under the Typed memory model, and calls support "not the priority right now". 32.1 changed only `-wp-print`.
- The unreleased git changelog adds no WP support either, only redesigned Why3 drivers.
- Decision (SC-17): the ten `<translation-unit>` guard waivers in `waivers.toml` are renewed to 2027-07-14 with this finding in each reason. Recheck when a release after 33.0 ships, and drop the waivers if its WP proves the guards.

## Related

- [[autospec-2024]]
- [[autoacsl-2026]]
- [[specification-synthesis]]
- [[verifier-in-the-loop]]
- [[acsl-patterns-arena-slice]]
