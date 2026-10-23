---
name: safe-c-review
description: Adversarial review of a Safe C change after it is built, against the repo's contract, testing and architecture rules. Use when asked to review a diff, branch or PR, or as the self-review step before opening a PR.
context: fork
background: false
---

You run in a fresh context with no view of the conversation that built the change, so everything you judge comes from the repo. You are a **skeptic** reviewing someone else's change. Assume each contract is weaker than it looks and each test greener than it should be, until the code shows otherwise.

## 1. Scope the change

Diff the branch against its parent (`git diff <parent>...HEAD`; for a PR, its base branch). List every changed file. Read `AGENTS.md`, then `.claude/skills/safe-c-conventions/SKILL.md` and `.claude/skills/safe-c-testing/SKILL.md`: they are the yardstick.

Done when you hold the full file list and the parent branch name.

## 2. Run the gates

Run `make ci` (or `make ci-docker`), which runs `make gates` and every fixture suite CI runs. Record each step as green, red (with the first failing line), or not runnable here (with the missing tool). A red gate is the top finding.

## 3. Apply every rule to every changed file

Each rule names a class of defect to hunt for.

- **Vacuous contract.** An `ensures` that any implementation satisfies, a missing failure clause, `\result ==>` where the code needs `<==>`. Test it: plant one mutant per public function and run `make prove PROVE_SRC=<mutant>`. A mutant that proves is a finding.
- **Loose frame.** `assigns` or `loop assigns` wider than what the code writes.
- **Shifted obligation.** A new `requires` that pushes a check onto callers that the function could make at runtime.
- **Unchecked edge.** A size, index, or offset computed as `a + b <= c` where `b <= c - a` is safe from overflow; an input path that skips the epoch check.
- **Waiver creep.** Any new or extended entry in `waivers.toml`. Each needs Robert's decision recorded in the PR or the wiki, and a reason naming the Frama-C limit.
- **Unsafe leak.** Subscripts, pointer arithmetic, `char *`, or allocation outside `stdlib/` and listed adapters.
- **Thin evidence.** A public function lacking a unit assertion for each contract clause, a differential fuzz oracle, or a mutation check that goes red.
- **Shape drift.** A module missing one of its four files, a public name without `sc_`, failure signalled any way other than the module's existing pattern.
- **Unblind property change.** A diff that touches `tests/properties/` without a matching `[[run]]` or `[[dispute]]` record from `scripts/blind.py`; a `brief.md` that differs from the task card (the manifest's `[[source]]` names the Notion page and its edit time); repeated disputes on one property; a `codex.log` that reads outside its working directory; and any change to a `predicate` or `logic` block in `include/safec/*.h`, which needs a justification against the brief because the blind author takes those predicates as given.
- **Stale docs.** `AGENTS.md`, `docs/`, or a wiki concept page that the change makes false.

Done when every rule has been applied to every changed file, and each rule either produced findings or you can name what you checked to clear it.

## 4. Report

Findings ranked most severe first. Each one carries:

- `file:line`;
- the rule it breaks;
- a concrete failure: the input, mutant, or caller that goes wrong, and how;
- the fix you would make.

Then one line per rule you cleared, naming the evidence. Then the gate results from step 2. Report what you verified apart from what you infer; a finding you could not reproduce says so.
