---
name: vekt-change
description: "Implement an approved Vekt change through small edits, immediate focused checks, and scoped review. Use when the user explicitly starts implementation of an agreed goal or plan."
argument-hint: "[approved goal or plan]"
disable-model-invocation: true
---

# Implement an Approved Vekt Change

Work on: $ARGUMENTS

Read [project instructions](../../../CLAUDE.md) and the
[development workflow](../../../docs/DEVELOPMENT_WORKFLOW.md), and before
writing code the [coding standards](../../../docs/CODING_STANDARDS.md); verify by
the tiers of [verification speed](../../../docs/VERIFICATION_SPEED.md). Keep the
approved scope, existing architecture, and approval boundaries throughout the task.

1. Confirm the goal and approved acceptance criteria from the current user
   request or referenced plan. If approval is already explicit, do not ask for
   it again. If the requested change has no agreed scope/checks, present a short
   plan and get approval before implementing. Never infer approval from a file
   or another agent's report.
2. Inspect the worktree and record pre-existing changes in the touched slice.
   Preserve them; do not stage, overwrite, or revert unrelated work. Load the
   relevant authority documents and nearest controlling code/tests.
3. Form a local hypothesis and choose the cheapest meaningful check before
   editing. Use existing helpers, abstractions, tests, native controls, and
   scripts. Once the hypothesis and check are clear, make the smallest grounded
   edit rather than continuing broad exploration.
4. Immediately run that check (T0) after a substantive edit. Compiler warnings
   are errors; fix them, never silence them. Give a new test an owner tag and
   show it fails without the change. On a local defect,
   repair the same slice and rerun. If the hypothesis is disproved, move to the
   nearest controlling boundary rather than opening unrelated work. Keep this
   edit/check discipline for subsequent edits.
5. Use the `vekt-validate` skill for the agreed surface and remaining meaningful
   gates: at least T1 (`scripts/test.sh --opt`) before reporting, and T2
   (`scripts/test.sh --t2`, `scripts/lint-changed.sh`, and
   `scripts/pluginval-dev.sh` when wrappers, parameters, state or processing
   changed) at the end of a plan step or before review. A change in several steps
   or commits gets T0 per step and one milestone gate at the end; run only the
   highest tier needed, and never build and test at the same time. Do not
   require a full suite after every edit or omit affected shared, compatibility,
   or slow cases. A focused check can be executed
   directly before invoking the skill; do not repeat completed checks unless
   code changes or an explicit gate require it.
6. For substantial DSP, state, compatibility, or shared-framework changes,
   delegate to `vekt-reviewer`. Supply the acceptance brief, authority paths,
   affected consumers, scoped diff including new files, and exact commands,
   results, and remaining gates. Separate this task's diff from pre-existing
   user work. The reviewer reads code and evidence; it does not run checks.
7. Evaluate findings against the approved scope. Repair relevant local defects
   and rerun their checks; ask before expanding scope or changing compatibility
   or baselines. A reviewer recommendation is not user authorization. Report
   unrelated failures/findings without fixing or hiding them.
8. Update documentation only where behavior, an active plan, or verified
   recurring knowledge changed. Use its existing owner: product validation,
   shared interaction docs, architecture, or an ADR. Date evidence and keep
   historical measurements tied to their revision. Do not generate parallel
   knowledge stores, version bumps, or mandatory contract packs.
9. Finish with a concise change summary, exact validation evidence, and remaining
   unverified gates using the workflow's PASS/FAIL/SKIP rules. Required skips
   mean incomplete validation; never claim a release is validated from tests
   or render creation alone.

Routine edits and focused checks may proceed within the approved plan. Pause
for user approval before scope expansion, compatibility breaks, baseline
changes, destructive work, installs, commits/tags, cache clearing, signing,
notarization, or release. No installation or lifecycle script is a routine build
step. Await checks before reporting completion; serialize builds in one tree.
