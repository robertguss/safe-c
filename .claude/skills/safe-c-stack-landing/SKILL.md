---
name: safe-c-stack-landing
description: Restack the Safe C PR stack after a PR merges, so each child targets main or its live parent, carries the merged changes, and is green. Use when an SC PR lands, a stack PR shows a merge conflict, or a child still points at a merged branch.
---

Robert lands the stack bottom-up. Each landing leaves the next PR **orphaned**: its base is a branch whose work is now in `main`. This skill re-parents every orphan and brings it current.

## 1. Map the stack

List open PRs and, for each, its head branch, base branch, mergeable state, and latest CI result. Mark a PR orphaned when its base branch's PR has merged.

Done when every open SC PR has a row with those four facts.

## 2. Re-parent each orphan, lowest first

For each orphaned PR, in stack order:

1. Change its base to `main` (or to its parent if the parent is still open).
2. Merge the new base into its head branch. A **merge commit** keeps every existing checkout valid; use it on any branch you did not create. Resolve conflicts by keeping both sides' intent; when both sides changed the same logic, stop and put the choice to Robert.
3. Run `make gates` and the CI fixture suites, then push.
4. Move to the PR stacked on this one: it now needs this branch merged in too.

Done when no open PR's base is a merged branch, and every head contains its base.

## 3. Confirm green

Check CI on each pushed head. A red check goes through `safe-c-testing`'s "Reading a red gate". A check also red on `main` belongs to `main`; note it on the PR and fix it there.

Done when every open SC PR is green and mergeable, or its blocker is written on the PR.

## 4. Sync the hub

Run `safe-c-notion-sync`: the merged PR's task moves to Done, and "You are here" moves up the stack.
