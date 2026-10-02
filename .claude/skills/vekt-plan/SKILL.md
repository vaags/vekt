---
name: vekt-plan
description: "Plan a Vekt DSP, editor, state, preset, or build change with observable acceptance criteria and focused validation. Use when the user explicitly requests a plan before implementation."
argument-hint: "[goal]"
disable-model-invocation: true
---

# Plan a Vekt Change

Plan this request: $ARGUMENTS

Read [project instructions](../../../CLAUDE.md) and the
[development workflow](../../../docs/DEVELOPMENT_WORKFLOW.md), then only the
authority documents relevant to the touched surface. This invocation is
planning, not permission to implement.

1. Start from the named file, symbol, failing behavior, test, or command. If none
   is named, find one concrete owning path with a targeted search. Inspect the
   current worktree without modifying, staging, or reverting it.
2. Read enough local code and nearby tests to identify what controls the
   behavior, affected consumers, one falsifiable hypothesis, and the cheapest
   check that could disprove it. Do not map unrelated modules for reassurance.
3. Ask only questions whose answers change scope, observable behavior,
   compatibility, or acceptance. Make routine implementation choices consistent
   with the codebase. Do not impose a fixed questionnaire or phase structure.
4. Produce the workflow's change brief: intent, observable acceptance criteria,
   owner/consumers, compatibility impact, scope/exclusions, and focused checks.
5. Give an actionable sequence of small edits and discriminating checks.
   Identify actual test names/tags, matching targets/configurations, relevant
   slow/shared cases, and any measured or manual gates. Product labels alone
   are not sufficient, and a zero-test selection cannot validate the change.
6. Mark checks as planned, not passed. Explain missing infrastructure or
   evidence that prevents acceptance; do not substitute a build for listening,
   host registration, numerical assertions, or visual inspection.
7. Keep a routine plan inline. If a persisted multi-session plan is requested
   or needed, propose the existing documentation owner and obtain approval
   before writing it. Do not create competing specs, registries, or checkpoints.
8. Stop and request approval for the plan. Do not edit implementation files,
   run builds/tests/renders, install, alter baselines, commit/tag, bump versions,
   clear caches, or change settings as a side effect of planning.

For delegated research, pass the relevant constraints, authority paths,
acceptance intent, and read-only scope explicitly. A fresh subagent may not have
the conversation or project instructions. Research results do not authorize work.
