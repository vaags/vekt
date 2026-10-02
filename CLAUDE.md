# Vekt Development

Vekt is a reusable C++20 framework built on JUCE for developing native audio
effects and instruments. It provides shared DSP, state, preset, and native
editor infrastructure, with Rav, Glimmer, and Mono as concrete products built
on the framework. Current macOS builds target VST3, Standalone, and Audio Unit
v2 (AUv2), using the existing Ninja or Xcode presets.

Keep its existing architecture, pinned JUCE dependency, CMake presets, and
scripts; do not introduce a parallel plugin framework or WebView UI as part
of routine development.

- Keep reusable, product-independent functionality in the framework; keep
  product-specific algorithms, parameters, and composition in the owning plugin.
- Reuse existing framework components when developing products. Introduce new
  abstractions only for at least two concrete consumers or a hard ownership boundary.

## Read Only What Applies

- [Architecture](docs/ARCHITECTURE.md): dependency direction, real-time and compatibility rules.
- Product contracts: [Rav](docs/RAV_VALIDATION.md), [Glimmer](docs/GLIMMER_VALIDATION.md), [Mono](docs/MONO_VALIDATION.md).
- Shared interactions: [UI](docs/UI_UX.md), [presets](docs/PRESET_UX.md).
- [Development workflow](docs/DEVELOPMENT_WORKFLOW.md): check selection, commands, evidence, and approvals.
- For active Mono ladder work, consult [its acceptance plan](docs/MONO_LADDER_ACCEPTANCE_PLAN.md).

Load the touched slice and its authority documents, not every document on
every task. Inspect actual code and tests; dated evidence is not a fresh run.
Surface specification/code conflicts rather than silently changing a contract.

## Scope and Approval

- Approve a scoped plan, then iterate through routine edits and focused tests.
- Ask before expanding scope, breaking compatibility, changing baselines,
  committing/tagging, installing, clearing caches, deleting work, or signing,
  notarizing, or releasing. Only the user can authorize these operations.
- Preserve dirty user changes and unrelated files; do not revert or stage them.
- Do not auto-commit, bump versions, install plugins, or alter permissions/hooks.
- A small fix needs a brief inline plan, not a new contract pack or status registry.

## Implementation and Checks

- Start at the controlling code path and a nearby test. Form a local hypothesis
  and the smallest check that could disprove it, then edit.
- After a substantive edit, run the cheapest meaningful check before expanding
  scope. Repair locally and rerun; reuse existing APIs, helpers, and tests.
- Use the check-selection matrix in the development workflow. Product tags are
  incomplete, especially Rav and editor cases; inspect actual selected tests.
- Reject zero-test selections. Cover shared consumers and relevant slow tests.
- Use Release for performance evidence. Renders and pluginval do not replace
  measured audio assertions, visual checks, listening, or host acceptance.
- Report exact commands and `PASS`, `FAIL`, or `SKIP` with reasons. A required
  skipped check means incomplete validation. Never claim unexecuted checks passed.
- Keep pre-existing failures visible without repairing unrelated code.

## Claude Workflows

- `/vekt-plan <goal>`: explicit planning; stop for approval before implementation.
- `/vekt-change <approved goal or plan>`: explicit implementation and verification.
- `/vekt-validate <rav|glimmer|mono|shared> <scope>`: user- or model-invoked checks.
- Use `vekt-reviewer` for substantial DSP, state, compatibility, or shared changes.
  Supply the scoped diff, acceptance criteria, authority paths, exact check
  commands/results, and remaining gates. It is read-only and cannot run checks.
- Give every delegated task its relevant constraints and source-of-truth paths;
  do not assume a fresh subagent has the conversation or project instructions.
- Capture verified recurring lessons in the existing documentation owner; date
  evidence. Keep an active multi-session plan's gate, blockers, and next action current.
