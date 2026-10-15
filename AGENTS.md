# Agent rules

Safe C is a standalone C library. Any C project can adopt it.

Work is done only when `make gates` passes. The gates run in order. Lint, then sanitizer tests, then fuzz, then Frama-C WP.

Every public function in the standard library has an ACSL contract and a fuzz harness.

Unsafe C lives only in `stdlib/`. Other code stays inside the subset in `docs/subset-v1.md`.

A proof gap is allowed only as a waiver in `waivers.toml`. A waiver names an id, a file, a function, a goal, a reason, an owner, and an expiry date. An expired waiver fails the build. The file starts with no waiver entries.
