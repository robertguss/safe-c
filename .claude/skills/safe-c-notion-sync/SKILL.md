---
name: safe-c-notion-sync
description: Bring the Safe C Notion hub up to date with live GitHub state. Use after a PR opens, merges or changes CI state, after Robert makes a decision, or when asked to update Notion or the hub.
---

The Notion hub is Robert's single view of Safe C. GitHub is the source of truth; the hub is its summary, and it **drifts** every time a PR moves.

## Where things are

| What | Notion ID |
| --- | --- |
| Hub page "Safe C" (Dev Hub > Projects) | `3fa700320c4581639eaccecf329125ae` |
| Tasks DB, inline on the hub (Name, Status, Depends on, Review gated, PR link) | `collection://282f2f16-dc6f-4d04-b08b-64da7fb53f10` |
| Child page "Safe C plan (full)" | `3fa700320c4581679b4cf24dc6f3f044` |

## 1. Read live state

From GitHub, for every SC PR: state (open, draft, merged, closed), head SHA, base branch, mergeable state, and the latest check results. Read the merge commit SHA of each merged PR. Read the hub and the Tasks DB fresh. A summary from memory or an earlier session is a lead to check, never a fact to copy.

Done when you hold the live row for every PR the hub mentions.

## 2. Find the drift

Compare each of these hub spots with the live state. These go stale first:

- the "You are here" callout, the milestone paragraph under it, and its "Updated" date;
- "Next step" and "Waiting for Robert";
- the Status cells in the "PR order and dependencies" table;
- each task's Status and PR link in the Tasks DB;
- the milestone Status column;
- "Open decisions for Robert" and the dated "Decisions" list;
- any note saying CI is pending, red, or conflicting, and any PR described as targeting a branch that has since merged.

## 3. Fix

Rewrite each clearly stale fact to match GitHub, with the PR number, short SHA, and date. Write the "Updated" date as today. A decision Robert made goes into "Decisions" as a dated line naming him. Write the hub in Robert's plain style: full sentences, no internal session or tool names.

A fact you cannot settle from GitHub (a review Robert may have given offline, a decision implied but never stated) stays as it is; list it for Robert.

Done when every spot in step 2 matches the live state, or is on the list for Robert.

## 4. Report

Tell Robert in two or three lines what changed on the hub, then the open list.
