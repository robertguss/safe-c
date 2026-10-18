---
name: safe-c-notion-sync
description: Bring the Safe C Notion hub up to date with live GitHub state. Use after a PR opens, merges or changes CI state, after Robert makes a decision, or when asked to update Notion or the hub.
---

The Notion hub is Robert's single view of Safe C. GitHub is the source of truth for code; the hub is its summary, and it **drifts** every time a PR moves.

The Tasks DB is the only place task specs and status live (Robert, 17 Oct 2026). Each card holds its scope, files, done-when checklist, dependencies, review flag and PR link. Never add a task table or checklist anywhere else on the hub.

## Where things are

| What | Notion ID |
| --- | --- |
| Hub page "Safe C" (Dev Hub > Projects), an index of links only; never add content to it | `3fa700320c4581639eaccecf329125ae` |
| Status sub-page: "You are here", "Next step", "Waiting for Robert", milestones, proof totals | `3fd700320c458109a2b7cb676dcd666b` |
| Changelog sub-page: one line per merged PR, newest first | `3fd700320c458135a029fbe4c76cb1d3` |
| Decisions sub-page: Robert's dated decisions | `3fd700320c4581f4bd7ee73651aa14e1` |
| Tasks DB, linked from the hub (Name, Status, Depends on, Review gated, PR link) | `collection://282f2f16-dc6f-4d04-b08b-64da7fb53f10` |
| Archive sub-page, holding "Safe C plan rev 2 (archived)"; never edit or sync anything in it | `3fd700320c4581df9b90f476db29912c` |

The Overview, Plan and Mo pilot sub-pages are reference material; change them only when the plan itself changes.

## 1. Read live state

From GitHub, for every SC PR: state (open, draft, merged, closed), head SHA, base branch, mergeable state, and the latest check results. Read the merge commit SHA of each merged PR. Read the Status, Changelog and Decisions pages and the Tasks DB fresh. A summary from memory or an earlier session is a lead to check, never a fact to copy.

Done when you hold the live row for every PR the hub mentions.

## 2. Find the drift

Compare each of these spots with the live state. These go stale first:

- on the Status page, the "You are here" callout, "Next step", "Waiting for Robert", the milestone Status column, the proof totals, and the "Updated" date;
- each task's Status and PR link in the Tasks DB, and the card body: a merged task gets a **Result:** section with its PR, merge SHA, date and any deviation from its spec, and its done-when boxes ticked where evidence exists;
- the Changelog: every merged PR has a line;
- the Decisions page;
- any note saying CI is pending, red, or conflicting, and any PR described as targeting a branch that has since merged.

## 3. Fix

Rewrite each clearly stale fact to match GitHub, with the PR number, short SHA, and date. Keep the "You are here" callout to three sentences of current state; PR history goes on the Changelog as one line per PR (task, PR link, short SHA, one-sentence result) under its date. Write the "Updated" date as today. A decision Robert made goes on the Decisions page as a dated line naming him. Write the hub in Robert's plain style: full sentences, no internal session or tool names.

A fact you cannot settle from GitHub (a review Robert may have given offline, a decision implied but never stated) stays as it is; list it for Robert.

Done when every spot in step 2 matches the live state, or is on the list for Robert.

## 4. Report

Tell Robert in two or three lines what changed on the hub, then the open list.
