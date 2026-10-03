# Development Workflow

This is Vekt's project-scoped Claude Code workflow. It adapts planning,
focused iteration, review, and knowledge capture from
[Plugin Freedom System](https://github.com/glittercowboy/plugin-freedom-system),
not its build system, WebView UI, lifecycle scripts, or plugin registry.

## Authority and Scope

[ARCHITECTURE.md](ARCHITECTURE.md) owns dependency direction, real-time rules,
and compatibility policy. Read the relevant product contract:
[Rav](RAV_VALIDATION.md), [Glimmer](GLIMMER_VALIDATION.md),
[Kobber](KOBBER_VALIDATION.md), or [Flint](FLINT_VALIDATION.md). Shared editor and preset interactions belong to
[UI_UX.md](UI_UX.md) and [PRESET_UX.md](PRESET_UX.md). How code is written
(general, C++20 and JUCE practice) belongs to
[CODING_STANDARDS.md](CODING_STANDARDS.md); how checks are run quickly, to
[VERIFICATION_SPEED.md](VERIFICATION_SPEED.md).

Inspect current code, tests, and command options before using a recipe. Dated
measurements describe their recorded revision, not the present code. Surface
conflicts between a contract and implementation instead of silently changing
either or cleaning up unrelated documentation.

An approved plan authorizes routine scoped edits and focused checks. Ask before
expanding scope, breaking compatibility, changing baselines, committing or
tagging, installing plugins, clearing caches, deleting work, or signing,
notarizing, or releasing artifacts. Preserve existing user changes. Approval
must come from the user, not a subagent or a checkpoint file.

These instructions guide Claude; they are not deterministic enforcement.
The enforced parts are compiler warnings as errors in Vekt targets and a git
pre-commit hook that runs clang-tidy on staged framework and plugin sources
(`.pre-commit-config.yaml`, installed per clone with `pre-commit install`).
No Claude hooks, permission bypasses, automatic commits, version bumps, or
installation side effects are part of this workflow.

## Commands

| Command | Purpose | Invocation |
| --- | --- | --- |
| `/vekt-plan <goal>` | Produce a scoped plan and stop for approval | User only |
| `/vekt-change <approved goal or plan>` | Implement, check, and review approved work | User only |
| `/vekt-validate <rav\|glimmer\|mono\|flint\|shared> <scope>` | Select and execute relevant existing checks | User or Claude |

The `vekt-reviewer` agent reviews substantial DSP, state, compatibility, and
shared-framework changes in a fresh context. Its tools are only Read, Grep,
and Glob. The main agent supplies a scoped diff, acceptance criteria, authority
documents, and actual check output; the reviewer cannot execute checks or edit.
A fresh perspective is useful, but it is not independent proof of correctness.

## Change Brief

For an ordinary fix, keep this inline in the conversation:

- Goal and sonic or interaction intent.
- Observable acceptance criteria, including manual acceptance where needed.
- Owning code path and affected shared consumers.
- Parameter, state, preset, automation, or host compatibility impact.
- Included scope and explicit exclusions.
- Smallest check that can disprove the proposed fix; remaining required gates.

Use small edits followed immediately by the cheapest meaningful check. Once a
local hypothesis and discriminating check are clear, implement rather than
continue broad exploration. Repair a local failure and rerun the same check.
Expand validation when shared behavior or a user-facing workflow warrants it.

Multi-session work may need a persisted plan. Prefer its existing owner, such
as [KOBBER_LADDER_ACCEPTANCE_PLAN.md](KOBBER_LADDER_ACCEPTANCE_PLAN.md), rather than
a second status registry. Record approved scope, current gate, blockers,
dated evidence, and next action. Create another plan only when persistence is
needed and no existing document fits; no contract pack is required for a small fix.

## Check Selection

| Changed surface | Minimum relevant evidence |
| --- | --- |
| Product-local DSP | Nearby primitive and processor assertions; relevant audio measurements and listening gates |
| Shared DSP, state, or presets | Shared tests and all affected consumers; compatibility fixtures when relevant |
| Plugin support (`framework/plugin_support`) | `-L plugin-support`, then every product's preset, state, quality and compat cases (`-L presets`, `-L state`, `-L quality`, `-L compat`) and the Rav/Glimmer/Kobber/Flint processor tests |
| Editor or shared control | Control, layout, and value-format tests; affected editor builds and relevant visual/interaction checks |
| Parameters or host integration | Parameter/state/automation tests and matching format builds; runtime validator when required |
| Build or wrapper configuration | Affected products and formats; no-op/reconfiguration checks when relevant |
| Documentation or customization | References, command/configuration validity, and discovery/behavior checks; no audio rebuild solely for Markdown |

Select tests from nearby source and inspect the discovered matches. Catch2 tags
are CTest labels. Every test case carries an owner tag (a product, or a framework
module such as `[dsp]` or `[ui]`), and a case named after a product carries that
product's tag; the `Every test case carries a product or framework tag` test
enforces it (3 October 2026). A product label selects only the cases tagged with
the product, not the preset, editor and framework tests that also exercise it:
`scripts/test-affected.sh` adds those from the test files that use the changed
code, and still works at label and file granularity. Relevant slow tests must run even during
an otherwise quick iteration. A zero-test run fails.

### Tests and Builds

From the repository root, build tests before listing or running them:

```sh
cmake --preset dev
cmake --build --preset dev --target vekt_dsp_tests
ctest --preset dev -N -R '^Glimmer preserves APVTS project state$'
ctest --preset dev -R '^Glimmer preserves APVTS project state$' --no-tests=error
```

This is one exact-name example, not a general Glimmer acceptance gate. Choose
the actual nearby cases for the change. Use names or verified labels; include
shared cases. Record the executed test count.

| Product | Standalone target | VST3 target | AUv2 target | Offline renderer |
| --- | --- | --- | --- | --- |
| Rav | `Rav_Standalone` | `Rav_VST3` | `Rav_AU` | `VektRender --product rav` |
| Glimmer | `Glimmer_Standalone` | `Glimmer_VST3` | `Glimmer_AU` | `VektRender --product glimmer` |
| Kobber | `Kobber_Standalone` | `Kobber_VST3` | `Kobber_AU` | `VektKobberRender --fixture <name>` |
| Flint | `Flint_Standalone` | `Flint_VST3` | `Flint_AU` | none yet; `VektFlintCost` measures cost (A19) |

Build only required wrappers, for example:

```sh
cmake --build --preset dev --target Glimmer_VST3
```

Choose the tier from [verification speed](VERIFICATION_SPEED.md):
`scripts/test-affected.sh` for a change's owning tests, `./scripts/test.sh --t2`
at milestones and `./scripts/test.sh` for the full Debug suite when required. `dev-quick` is a test
preset, not a configure preset; it excludes `[slow]`. `release` and
`audio-lab-release` disable tests. Serialize builds using the same build tree;
check for a running watcher before starting another build.

`dev-opt` (`RelWithDebInfo`, `build/dev-opt`) builds the same tests optimised,
without `jassert` and without the reference renders; when to use which build is
in [verification speed](VERIFICATION_SPEED.md).

### Test Cost and Speed

[Verification speed](VERIFICATION_SPEED.md) owns the verification tiers (T0
focused, T1 `scripts/test-affected.sh`, T2 `scripts/test.sh --t2` with
`scripts/lint-changed.sh` and `scripts/pluginval-dev.sh`, T3 the full Debug
suite), the choice of build, test-time budgets and how to write cheap tests.
Pick the tier there; this document's matrix decides what the evidence must cover.

`zsh scripts/build-dev.sh` builds all nine supported development wrappers.
`zsh scripts/build-au.sh [--release]` builds all three AUv2 components with the
existing Ninja presets, without installing plugins. Xcode remains optional.

### Audio Evidence

Use Release for performance-sensitive evidence. The existing
`scripts/render-report.sh` uses the Debug `audio-lab` preset; the Kobber report
script builds multiple fixtures and a prototype. For a focused Release render:

```sh
cmake --preset audio-lab-release
cmake --build --preset audio-lab-release --target VektRender
output_directory=$(mktemp -d "${TMPDIR:-/tmp}/vekt-validation.XXXXXX")
build/audio-lab-release/tools/audio_lab/VektRender \
  --product glimmer --source sine --seconds 1 --warmup 0.25 \
  --profile tracking --quality 2 --frequency 440 \
  --sample-rate 48000 --block-size 127 --seed 1299148399 \
  --param glimmer.cabinetModel=Wide \
  --wav "$output_directory/glimmer.wav" --report "$output_directory/glimmer.json"
```

For Rav, use `--product rav` and appropriate Rav controls instead of Glimmer
overrides. Record all settings and consult the current parser for supported
values; these are example inputs, not universal acceptance settings. Kobber uses
a separate executable and requires both output paths:

```sh
cmake --build --preset audio-lab-release --target VektKobberRender
build/audio-lab-release/tools/audio_lab/VektKobberRender \
  --fixture filter-sweep --sample-rate 48000 --block-size 127 --seed 1299148399 \
  --wav "$output_directory/kobber.wav" --report "$output_directory/kobber.json"
```

The Kobber example follows the configure/output-directory setup above. Preserve
paths to evidence needed beyond the session; temporary files are not archival
storage. Fixed inputs make audio comparisons repeatable, not callback timing.
Generating a report successfully does not prove acceptable sound, aliasing,
latency, or CPU load. Check the relevant assertions or documented thresholds;
do not invent acceptance thresholds. Manual listening requires safe monitoring
and remains unrun until actually performed.

### Runtime and Release Boundaries

For a required VST3 runtime check, locate the actual matching artifact and run
pluginval with strictness 10. First check `command -v pluginval`, then the
executable at `/Applications/pluginval.app/Contents/MacOS/pluginval`. Check
bundles as directories. Identify the exact product, configuration, revision,
and path; an old installed bundle is not evidence for a new build.

`zsh scripts/validate-release.sh <rav|glimmer|kobber|flint> [vst3-path au-component-path]`
defaults to that product's Release artifacts. Follow the
[macOS release gates](../packaging/macos/README.md) for matching installed AU
components, runtime validators, manual host checks, and distribution approvals.
Builds or VST3 success do not establish AUv2 behavior or sandbox preset access.

## Evidence and Completion

List selected checks and their purpose before running them. For each check,
report the exact command, product/configuration/artifact, outcome, executed
test count or measured assertion, and failure/skip reason or evidence path.

- `PASS`: the check executed and met its defined criteria.
- `FAIL`: the check executed and failed, crashed, or selected no tests.
- `SKIP`: the check was not executed; state why and whether it is required.

A skipped required gate means incomplete validation, never an overall pass.
An irrelevant gate can be noted as not applicable with a reason; it is not a
pass. Keep compile success, assertions, audio evidence, visual checks, and
manual host/listening acceptance separate. Await completed commands before
reporting results. Report pre-existing failures without hiding them or fixing
them outside scope. State precisely what is verified and what remains unverified.

Capture verified recurring knowledge in its existing owner: architectural
decisions in `docs/decisions`, cross-cutting rules in the architecture, product
behavior/evidence in its validation document, and interaction rules in UI/preset
docs. Date evidence; do not promote session guesses or rewrite historical results.

## Pilot and Reassessment

3 October 2026: coding standards, verification tiers, owner tags on every test,
warnings as errors, ccache, precompiled test headers and the clang-tidy commit
hook were introduced. Reassess the tier selection (missed failures caught only
by T2 or T3) and the hook's cost on commits during the pilots below.

Two real tasks remain to be selected and authorized: one Kobber DSP change and
one shared state/preset/UI change. Assess missed tests, incorrect commands,
coverage of shared consumers, review usefulness, extra prompts, and document
churn. Tighten these skills first. A product-aware runner, durable structured
reports, or enforced hooks require a separate scope decision after the pilots.

Claude configuration details follow the current official
[skills](https://code.claude.com/docs/en/skills) and
[subagent](https://code.claude.com/docs/en/sub-agents) documentation. Check
installed-version support and discovery when adopting or changing these files.
