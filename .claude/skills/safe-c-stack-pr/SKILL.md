---
name: safe-c-stack-pr
description: Build the next Safe C stack PR (SC-06, SC-07 ...) from task to open pull request. Use when asked to start, build or ship an SC task or the next PR in the stack.
---

The Safe C plan ships as a linear stack of PRs from the Notion Tasks DB. Each PR targets its parent's branch, and Robert lands them bottom-up. Agents commit, push, and open PRs freely.

## 1. Load the task

Find the task in the Notion hub's Tasks DB (the `safe-c-notion-sync` skill names the IDs) and its section on the "Safe C plan (full)" page; `docs/project-sketch.md` carries the design intent. Read the research wiki concept pages the task touches (see the `research-wiki` skill).

If the task's "Review gated" box is checked, the design is Robert's call: post the options and your recommendation to him before building past a prototype, and record his decision in the wiki once he makes it.

Done when you can state the task's deliverables, its "Depends on" task, and whether it is review gated.

## 2. Branch from the parent

The parent is the "Depends on" task's branch while its PR is open, and `main` once it has merged. Fetch it fresh. Name the branch `sc-NN-<topic>`, unless your session assigns one.

## 3. Build

Declare, then run `scripts/blind.py`. For a task that adds or changes a proved function, first commit the header declarations with no contract, then run `python3 scripts/blind.py author <module> --code-model <your model>` (`update` for a module that already has a manifest). It writes `tests/properties/<module>/` from the task brief, which you save verbatim as its `brief.md`, before any contract exists. Never edit a manifest or its `props.c` by hand: dispute a property you think is wrong with `blind.py dispute`, and its answer stands.

Write the module following `safe-c-conventions`, and its evidence following `safe-c-testing`. Commit in steps that each leave the tree building.

Done when `make check` passes, plus the checks the table in `AGENTS.md` names for this change (`make ci-docker` where Frama-C is not installed), or each check you cannot run here is named with the tool it lacks. Run the full `make ci` only when Robert asks.

## 4. Self-review

Run the `safe-c-review` skill. It runs in a fresh context, so it judges the change cold. Fix each finding, or write down why it stands.

Done when every finding is fixed or answered, and the same checks pass again after the fixes.

## 5. Open the PR

- Base: the parent branch from step 2.
- Title: `SC-NN: <what it adds>`.
- Body: the repo has no PR template, so open with a "Before:" and an "After:" paragraph, then a short "How". For a proof-bearing PR, give the goal count from `build/prove.log` (proved, waived, unproved) and the mutants WP rejected. Give each module's property count from `manifest.toml`, its disputes and their decisions, and any property a fallback model wrote. List any waiver the PR adds with the decision behind it, and which checks ran and which did not. Perf numbers are recorded, not gated.

Then run `safe-c-notion-sync` so the hub shows the PR as in review.
