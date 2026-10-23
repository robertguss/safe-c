---
title: Manual-Only Checks - Plan
type: chore
date: 2026-10-22
topic: manual-only-checks
artifact_contract: ce-unified-plan/v1
product_contract_source: ce-plan-bootstrap
execution: code
---

# Manual-Only Checks - Plan

## Goal Capsule

- **Objective:** Robert and the agents working on Safe C can push, open PRs and call work done without waiting on any check. Every check the repo has today can still be run by hand when someone decides it is worth the time.
- **Means:** GitHub workflows become manual-dispatch only (KTD1). The "done only when gates pass" rule in `AGENTS.md`, the Safe C skills and the docs becomes a fast default plus a menu of on-demand checks (KTD2, KTD3).
- **Product authority:** Robert.
- **Execution profile:** one PR stacked on `sc-27-property-manifest` (KTD4). Agents commit and push, and Robert merges.
- **Stop conditions:** stop and ask before deleting any check script, test, proof, model, fixture or workflow file. This plan changes only workflow triggers, rules, prose and the new `make check` target. No existing check target, script body or test changes. Also stop before touching repo settings on GitHub (disabling workflows, branch protection).

---

## Product Contract

### Summary

Nothing runs on push, on pull request or on a schedule. The checks stay in the tree, and `make` targets and GitHub's "Run workflow" button run them on demand. The repo's written rules stop treating a full green run as the bar for done. They name a fast default (`make check`: lint plus the sanitizer unit tests) and say which heavier check fits which kind of change.

### Problem Frame

Every push to a PR starts the Gates workflow. It runs the whole `scripts/ci.py` list (proofs, property clients, TLC, TLAPS, GenMC, fixture suites) plus six mutation shards, and a run takes 30 to 110 minutes. The last four Gates runs on 2026-10-22 were all cancelled before they finished, because a newer push superseded each one. The agent rules make it worse. `AGENTS.md`, `safe-c-stack-pr`, `safe-c-stack-landing`, `safe-c-review` and `safe-c-testing` each say work is done only when `make ci` (or `make gates`) is green, so every agent change also waits on the full local suite. That cost now outweighs what the checks catch during day-to-day development. Robert wants to choose when to pay it.

### Requirements

**Automation**

- R1. No GitHub workflow in this repo runs on push, pull request or schedule.
- R2. Each existing workflow except the reusable one can still be started by hand from GitHub, with the inputs it takes today (for example the nightly fuzz `fuzz_seconds`).
- R3. The reusable adopter workflow keeps its interface unchanged, because other repos call it.

**Local checks**

- R4. Every check that exists today keeps its `make` target or script and still runs by hand: lint, sanitizer tests, TSan, fuzzing, prove, properties, mutate, model, TLAPS, GenMC, the fixture suites, `make gates`, `make ci` and `make ci-docker`.
- R5. One short command runs the fast default checks.

**Written rules**

- R6. No agent rule, skill or doc says work is done only when gates or CI are green, or that a run must pass before pushing.
- R7. The agent rules name the fast default and say which on-demand check fits which kind of change, so an agent can pick the right heavier check without running all of them.
- R8. Docs that describe CI as automatic now describe it as manual.

### Scope Boundaries

- No check code, test, proof, model, fixture, waiver or evidence file is deleted or weakened.
- `docs/evidence/` stays untouched. It records what past runs showed, and rewriting it would falsify history.
- The adopter quickstart's "Put the gates in CI" section stays. It tells adopters how to call the reusable workflow from their own repos, which is unaffected.
- Considered and not built: a pre-push hook or a lighter automatic CI tier. The request was for nothing to run automatically. Revisit this if regressions start reaching `main` unnoticed.
- Considered and not built: speeding up the checks themselves (caching the formal toolchain, trimming fixture suites). That is a separate optimization. Manual runs make it less urgent.

#### Deferred to Follow-Up Work

- The Notion hub may still describe the done rule as "gates green". `safe-c-notion-sync` can update it after this merges.

---

## Planning Contract

### Key Technical Decisions

- KTD1. **Keep the workflow files and switch their triggers to `workflow_dispatch` only. Don't delete them.** (session-settled: user-approved — chosen over deleting the workflows outright: a pinned-toolchain run stays one click away without installing Frama-C locally.) `gates.yml`, `nightly.yml`, `adopter.yml` and `ci-image.yml` lose their `push`, `pull_request` and `schedule` triggers. `gates.yml` keeps `workflow_call` too. Nothing calls it today, but the trigger is inert and costs nothing. Concurrency and `pull_request`-referencing expressions keep working under dispatch, so they don't need edits.
- KTD2. **`safec-adopter.yml` stays as it is.** (session-settled: user-approved — chosen over folding it into the manual-only change: other repos call it, and it never runs on this repo's pushes.) `adopter.yml` calls it through its `sample-adopter` job, so a manual `adopter.yml` run still exercises it.
- KTD3. **The fast default is a new `make check` = `make lint` + `make test`.** (session-settled: user-approved — chosen over keeping `make ci` green as the bar: lint plus the sanitizer unit tests takes seconds to minutes and catches most day-to-day breakage.) Proofs, property clients, mutation and the formal tools run only when Robert asks or when the change calls for them, per the menu in `AGENTS.md` (U3). `make gates` and `make ci` stay as manual bundles under their current names, so existing muscle memory and `ci.py` step selection still work.
- KTD4. **Land as a child of `sc-27-property-manifest`, not as a fresh branch from `main`.** That branch already edits `AGENTS.md`, `Makefile`, `scripts/ci.py`, `docs/guide/contracts.md` and three of the Safe C skills, so a branch from `main` would conflict on nearly every file. Once the change is on `main`, PR runs stop as well. GitHub reads triggers from the PR's merge ref, and none of the open branches edit workflow files.

### Sources

- `.github/workflows/gates.yml` (push and pull_request triggers, six `mutate` shards, the `gates` aggregate job). Runs on 2026-10-22 took 2 to 110 minutes before cancellation.
- `.github/workflows/nightly.yml` (daily cron), `adopter.yml` (push and pull_request), `ci-image.yml` (path-filtered push and pull_request).
- `main` has no branch protection or rulesets, so no required check blocks merging once the workflows stop running.
- No test under `tests/` reads `.github/workflows/`. `scripts/ci.py` only builds the step list, and its unregistered-test guard stays useful for manual runs.

---

## Implementation Units

### U1. Workflows run only on manual dispatch

- **Goal:** No workflow runs automatically. Each one can be started from GitHub's Actions tab.
- **Requirements:** R1, R2, R3 (KTD1, KTD2)
- **Dependencies:** none
- **Files:** `.github/workflows/gates.yml`, `.github/workflows/nightly.yml`, `.github/workflows/adopter.yml`, `.github/workflows/ci-image.yml`
- **Approach:**
  1. In `gates.yml`, remove `push` and `pull_request`, and keep `workflow_dispatch` and `workflow_call`. Give `workflow_dispatch` the same `fuzz_seconds` input `workflow_call` has, so a manual run can pick the budget.
  2. In `nightly.yml`, remove `schedule` and keep `workflow_dispatch` with its input. Rename the workflow to something like "Fuzz (long)", since it no longer runs nightly.
  3. In `adopter.yml` and `ci-image.yml`, remove `push` and `pull_request` and keep `workflow_dispatch`. In `ci-image.yml`, image publishing is gated on `github.event_name != 'pull_request'`, so a manual run on `main` still publishes the image `make ci-docker` pulls.
  4. Update each file's header comment to say it runs on manual dispatch.
- **Patterns to follow:** the existing `workflow_dispatch` blocks in `nightly.yml` and `gates.yml`.
- **Test scenarios:** Test expectation: none -- trigger-only config change. Verified as described below.
- **Verification:** each changed file parses as YAML. `actionlint` passes on it if installed. After the push, `gh run list` shows no new run for that commit. `gh workflow run gates.yml --ref <branch>` would start one, but run it only if Robert asks.

### U2. `make check` and stale CI comments in the build scripts

- **Goal:** one command runs the fast default, and build-script comments stop describing automatic CI.
- **Requirements:** R4, R5 (KTD3)
- **Dependencies:** none
- **Files:** `Makefile`, `scripts/ci.py`, `ci/Dockerfile`
- **Approach:**
  1. Add a `check` target that runs `lint`, then `test`, in that order even under `make -j`, the same way `gates` keeps lint first. Add it to `.PHONY`.
  2. Reword comments that say CI runs a target automatically (the `genmc`/`tlaps`, `mutate` and `ci` blocks). They should describe `make ci` as "every check, run by hand".
  3. Change the `scripts/ci.py` docstring from "every check the Gates workflow runs" to the same framing. Step logic stays unchanged.
  4. Update the `ci/Dockerfile` header comment so it doesn't imply a workflow runs on every push.
- **Patterns to follow:** the `gates: lint` dependency style in `Makefile`.
- **Test scenarios:**
  - `make check` on a clean tree runs lint, then the sanitizer tests, and exits 0.
  - `make check` exits non-zero when lint fails, and the tests don't run.
  - `python3 scripts/ci.py --list` prints the same step list as before.
- **Verification:** `make check` passes locally, and `make -n gates` and `make -n ci` show unchanged commands.

### U3. AGENTS.md: fast default and an on-demand menu

- **Goal:** the agent rules stop requiring a green full run and instead tell agents what to run when.
- **Requirements:** R6, R7 (KTD3)
- **Dependencies:** U2
- **Files:** `AGENTS.md`
- **Approach:**
  1. Replace "Work is done only when `make gates` passes" and "Run one of them green before pushing" with the new rule. `make check` is the default before calling a change done. The heavier checks below run when Robert asks or when the change calls for them, and a PR body names which checks ran.
  2. Add a short table that maps each kind of change to its on-demand check. Lint rules map to `make lint-selftest`. A contract or proved function maps to `make prove PROVE_SRC=<file>`, then `make properties` and `make mutate MUTATE_SRC=<file>`. Channel or mailbox code maps to `make tsan`, then `make model`, `make genmc` and `make ci CI_STEPS="chan mailbox"`. Guide docs map to `make guide-test`. Packaging maps to `make package-test`. "Everything" maps to `make gates`, `make ci` or `make ci-docker`.
  3. Edit the sentences that say "CI runs it as …" or "CI splits it across six jobs" so they describe a manual run. Keep the timing facts (mutate takes about 27 minutes on 4 cores), because they help someone decide whether to run it. Note that the image `make ci-docker` pulls refreshes only on a manual CI image run on `main`. Otherwise `make ci-docker` falls back to a local build.
- **Patterns to follow:** `AGENTS.md`'s existing terse, command-first prose.
- **Test scenarios:** Test expectation: none -- prose. Covered by the stale-phrase sweep in the Verification Contract.
- **Verification:** `AGENTS.md` no longer says gates or CI must pass, and every `make` target the table names exists in `Makefile`, and every `CI_STEPS` name exists in `scripts/ci.py`.

### U4. Safe C skills follow the new rule

- **Goal:** the skills that build, review and restack PRs stop blocking on `make ci`.
- **Requirements:** R6, R7
- **Dependencies:** U3
- **Files:** `.claude/skills/safe-c-stack-pr/SKILL.md`, `.claude/skills/safe-c-stack-landing/SKILL.md`, `.claude/skills/safe-c-review/SKILL.md`, `.claude/skills/safe-c-testing/SKILL.md`, `.claude/skills/safe-c-conventions/SKILL.md`
- **Approach:**
  1. `safe-c-stack-pr`: in step 3, "done" becomes `make check` passing plus whichever on-demand checks the `AGENTS.md` menu names for the change. In step 4, "gates green again" becomes "the same checks pass again". In step 5, the PR body lists which checks ran and which didn't.
  2. `safe-c-stack-landing`: in step 2.3, run `make check` before pushing instead of `make ci`. Step 3 drops "confirm CI green", because there is no CI to wait on, and checks that each head merged cleanly and passes `make check`. The description loses "and is green".
  3. `safe-c-review`: step 2 runs `make check` plus the menu's checks for the changed files. A red result is still the top finding. The full `make ci` runs only on Robert's request.
  4. `safe-c-testing`: replace "green on that list is the bar" with a pointer to the `AGENTS.md` menu. The evidence rules stay unchanged, since every function still needs tests, a harness and a proof. Only when they run changes.
  5. `safe-c-conventions`: change the line describing `AGENTS.md` as holding "the done rule (`make gates`)".
- **Patterns to follow:** each skill's "Done when …" sentence style.
- **Test scenarios:** Test expectation: none -- prose. Covered by the stale-phrase sweep.
- **Verification:** no skill tells an agent to wait for CI or to treat `make ci` / `make gates` as required.

### U5. README and docs describe manual checks

- **Goal:** user-facing docs match the new reality.
- **Requirements:** R8
- **Dependencies:** U2
- **Files:** `README.md`, `docs/project-sketch.md`, `docs/guide/red-gates.md`, `docs/guide/README.md`, `docs/guide/contracts.md`
- **Approach:**
  1. `README.md`: in the CI bullet, say the workflows run on manual dispatch, with no nightly schedule. Add `make check` to the target list. Drop "Work is done only when `make gates` passes".
  2. `docs/project-sketch.md`: change the done-rule sentence and the `### CI` paragraph. Add no new history beyond what is needed.
  3. `docs/guide/red-gates.md`: change the intro's "Green on that list is the bar". The "CI only" section becomes advice for a manual workflow run or `make ci-docker` when a local toolchain differs.
  4. `docs/guide/README.md` and `docs/guide/contracts.md`: change "CI runs it as …" to the manual equivalent.
- **Patterns to follow:** existing guide tone. Don't add new code blocks, because any block in `docs/guide/` needs a tag for `make guide-test`.
- **Test scenarios:**
  - `make guide-test` still passes after the guide edits, since the guide-check scans these files.
- **Verification:** `make guide-test` is green, and `docs/evidence/` and the quickstart's adopter-CI section are unchanged.

### U6. Record the decision in the research wiki

- **Goal:** future sessions find why the gates became manual, so nobody quietly turns them back on.
- **Requirements:** R6
- **Dependencies:** U3
- **Files:** `research-wiki/log.md`, plus a decision page if `research-wiki/SCHEMA.md` calls for one
- **Approach:** follow the `research-wiki` skill's decision operation. Record the cost (30–110 minute runs, mostly cancelled), what changed (dispatch-only workflows, `make check` default), what stayed (every check), and what would justify revisiting it (regressions reaching `main`).
- **Patterns to follow:** recent `log.md` entries, such as the SC-27 entry.
- **Test scenarios:** Test expectation: none -- wiki prose.
- **Verification:** the entry is in `log.md`, and any new page passes the wiki lint the skill describes.

---

## Verification Contract

| Check | Applies to | Pass signal |
|---|---|---|
| `make check` | U2, whole change | exits 0 |
| YAML parse of each changed workflow (`actionlint` if available) | U1 | no errors |
| `python3 scripts/ci.py --list` | U2 | same steps as before the change |
| `make guide-test` | U5 | exits 0 |
| Stale-phrase sweep: `grep -rnE "done only when\|green before pushing\|is the bar\|Confirm green\|CI runs (it\|them)\|is green\|gates green\|Run .make ci"` over `AGENTS.md`, `README.md`, `docs/` (excluding `docs/evidence/` and `docs/plans/`) and `.claude/skills/` | U3–U5 | no hits that still describe a required or automatic run |
| `gh run list` after pushing the branch | U1 | no run triggered by the push |

The full `make ci` is not required for this change, which is the change's own premise.

---

## Definition of Done

- Every requirement R1–R8 holds, and each unit's Verification is met.
- No check script, test, proof, model, fixture, waiver or evidence file is deleted or weakened (`git diff --stat` touches only the files listed in the units).
- The PR body lists which checks ran (at least `make check` and `make guide-test`) and links this plan.
- No leftover or experimental edits remain in the diff.

---

## Operational Notes

- Branches without this change on their merge ref keep their old triggers. Once it merges, open PRs stop triggering as well (KTD4).
- For immediate relief before this merges, running `gh workflow disable` on all four (Gates, Adopter, CI image and Nightly fuzz, by their names before the rename) stops runs repo-wide at once. That changes a shared repo setting, so only Robert runs it or explicitly asks for it. He then re-enables all four after the merge, so manual dispatch works again.
