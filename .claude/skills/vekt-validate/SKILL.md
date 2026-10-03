---
name: vekt-validate
description: "Select and execute focused Vekt tests, builds, audio assertions, and required runtime checks. Use after approved changes or when asked to validate Rav, Glimmer, Kobber, Flint, or shared behavior; report incomplete gates explicitly."
argument-hint: "[rav|glimmer|kobber|flint|shared] [scope]"
disable-model-invocation: false
---

# Validate a Vekt Change

Validate: $ARGUMENTS

Read [project instructions](../../../CLAUDE.md) and the
[development workflow](../../../docs/DEVELOPMENT_WORKFLOW.md), especially check
selection, command recipes, runtime boundaries, and evidence rules, and
[verification speed](../../../docs/VERIFICATION_SPEED.md) for tiers and build
choice. These are the command reference; do not invent another build or
reporting system.

1. Establish the requested product/surface, acceptance criteria, and changed
   ownership/consumers. If arguments are absent, use the approved task and
   current scoped changes. Ask only when ambiguity affects check selection.
   This invocation authorizes scoped validation, not implementation or fixes.
2. Inspect nearby actual tests and configuration. Every test case carries an
   owner tag, but a product label omits the shared framework tests a change
   affects; `scripts/test-affected.sh --dry-run` shows the T1 selection for the
   changed paths. Use actual names/tags and affected shared consumers. Do not
   automatically substitute quick-suite success for relevant slow or
   compatibility tests.
3. State the selected checks, their tier and purpose before executing. Choose
   the cheapest meaningful check first and add only required risk-based gates:
   T1 for a reported change, T2 (`scripts/test.sh --t2`,
   `scripts/lint-changed.sh`, `scripts/pluginval-dev.sh` when wrappers,
   parameters, state or processing changed) at milestones, T3
   (`scripts/test.sh`) for release-level or build-wide changes.
   Reuse existing current results only when their revision, configuration,
   artifact, and check match; otherwise rerun. Do not run duplicate builds in
   a tree used by a watcher or another agent.
4. Build `vekt_dsp_tests` in `dev-opt` for speed or `dev` where Debug matters
   (`jassert` is compiled out of `dev-opt`; reference renders run only in `dev`).
   Inspect matching CTest cases with `-N` using the same filter, then
   execute with `--no-tests=error` and record the test count. Zero matches are
   a failed selection, not a pass; correct the filter before claiming coverage.
   Read current test/preset definitions instead of trusting an old recipe.
5. Build the matching product wrappers only when integration/acceptance needs
   them. Documentation-only changes do not require audio rebuilds. For audio
   evidence, use matching renderers and explicit inputs/settings; use Release
   for performance. Examine defined assertions or documented thresholds.
   Report generation is not sonic acceptance; timing is not deterministic.
6. For a required pluginval check, locate the tool on PATH or at
   `/Applications/pluginval.app/Contents/MacOS/pluginval`, check the actual
   matching built bundle as a directory, and run strictness 10. Do not silently
   validate an older installed artifact. The product-aware release script checks
   matching installed AUv2 contents and the effect/instrument tuple before
   registered AU validators; on-disk checks do not prove host-loaded identity.
   Keep registration, host smoke tests, signing and distribution separate.
7. If a required tool, artifact, threshold, graphical session, or manual
   listening/host observation is missing, mark its gate SKIP with a reason.
   Do not install tools/plugins, clear caches, register extensions, change
   baselines, or sign/notarize to manufacture a pass without user approval.
8. Await all checks. Report each exact command, product/configuration/artifact,
   status, executed test count or measured assertion, and relevant evidence
   path or failure/skip reason. Keep compile, automated behavior, measured
   audio, visuals, and manual acceptance separate.
9. Summarize scope precisely: PASS means executed and criteria met; FAIL means
   executed and failed, crashed, or selected no tests; SKIP means not executed.
   Required skips mean incomplete validation. Note irrelevant gates as not
   applicable with a reason rather than passed. Surface pre-existing failures
   and omitted coverage. Do not alter implementation or fixtures to make tests
   pass, nor claim overall acceptance from a partial run.

Keep evidence concise in the response. Use existing test/render logs and paths
when available; no persistent structured report runner or new script is part
of this initial layer. Reviewers can assess supplied evidence, not replace an
executed check or a user's manual acceptance.
