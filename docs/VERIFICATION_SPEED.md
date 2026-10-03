# Verification Speed

How to get trustworthy evidence quickly: which checks a change needs, which build to run them in, and how to keep
builds, tests and renders cheap. [DEVELOPMENT_WORKFLOW.md](DEVELOPMENT_WORKFLOW.md) owns what evidence a change
needs (the check-selection matrix) and how it is reported; this document owns how fast it is obtained. Speed never
replaces a required check: a skipped gate is reported as `SKIP` with its reason.

## Tiers

| Tier | When | Command | Typical time (3 October 2026) |
| --- | --- | --- | --- |
| T0 | Every edit: the cheapest check that could disprove the change | Exact test names or tags, e.g. `ctest --preset dev-opt -R '<name>' --no-tests=error`; a focused build of one target | Seconds |
| T1 | Before reporting a change: the tests of the changed code and of the code and tests that use it | `scripts/test-affected.sh` (`--dry-run` shows the selection and counts) | Rav 12 s (241 tests), Kobber 30 s (347 tests) |
| T2 | Milestones: end of a plan step, before review, before handing work back | `scripts/test.sh --t2`, `scripts/lint-changed.sh`, and `scripts/pluginval-dev.sh` when wrappers, parameters, state or processing changed | Tests 60 s; lint about 12 s; pluginval 5 s once built |
| T3 | Release-level, a shared or build-wide change, or when the user asks | `scripts/test.sh` (the full Debug suite) plus the release gates in DEVELOPMENT_WORKFLOW.md | Tests 80–88 s |

- **T1** maps changed paths (or `--base <rev>`, or explicit paths) to CTest labels:
  - a product directory selects that product and the framework labels of every test file using the product (its
    namespace or headers), such as the preset and editor tests that construct its processor;
  - a framework module selects its own tag, the modules that link it (`framework/*/CMakeLists.txt`), every product,
    and the labels of every test file using it;
  - a test file selects its own owner tags; packaging and scripts select the script tests; anything unmapped (CMake,
    presets, JUCE, external) or a deleted test file selects the full suite.

  It runs the selection in `dev-opt`, slow tests included, and always the `[compat]` tests in Debug, where the
  reference renders are captured and compared. Selection works at the granularity of labels and test files, so it
  relies on every registered test case carrying an owner tag (enforced by the
  `Every test case carries a product or framework tag` test). It can still miss a case whose only link to the
  changed code is indirect (a helper in another test file, a product reached through a framework interface): when a
  change's reach is unclear, run T2.
- **Hidden cases** (`[.]`) carry no owner tag: Catch2 runs a hidden case whenever a tag it carries is named, so
  `vekt_dsp_tests "[compat]"` would otherwise run the capture cases and rewrite the fixtures. The tag policy enforces
  this. Run hidden cases by their own tag or name only.
- **T2** runs the Debug suite without `[slow]` (assertions on, reference renders included), then the `[slow]` tests
  in `dev-opt`. Assertions inside slow tests therefore run only in T3.
- A change may need more than its tier: listening, visual checks, host checks and Release performance gates are not
  replaced by any tier.

## Proving the sound did not change

A change that must not alter the sound (a refactor, a type or structure change, a move between files) is proven with
`scripts/render-diff.sh <base>`: it renders the corpus (`tests/compat/RenderCorpusTests.cpp`, 42 cases across Rav,
Glimmer and Kobber, including polyphony, voice stealing, note priority, held-key return, the sustain pedal, MIDI
controllers, a parameter change mid-phrase, Multicore, odd block sizes and offline rendering) with the base revision and
with the working tree, and compares every case byte for byte. "42 of 42 cases identical" is the evidence; anything else
lists each differing case with its worst difference in dBFS. The corpus itself fails unless every case is
deterministic, audible and distinct from the others (or declares the case it must equal). The reference renders
cannot prove this: they allow differences up to about -94 dBFS.

- The base builds in a worktree cached under `build/render-diff/<commit>`: the first run is a cold build (about
  80 s in `dev-opt`, 2 min in `dev`), later runs about 17 s (3 October 2026). `--clean` deletes the cached worktrees
  and their builds.
- Both sides use the same build: `dev-opt` by default, `--preset dev` for Debug. Run both before relying on "no
  change" for a change that could round differently with or without optimisation.
- The base always renders this tree's corpus: it is copied into a base that lacks it or has another version, so any
  revision whose processors take the same parameter IDs can be compared. A base from before a rename of a product's namespace or
  headers (Mono to Kobber, 3 October 2026) cannot compile this corpus; compare across it by rendering each side's own
  corpus and mapping the case names. Add a case to the corpus when a behaviour it does not reach is being refactored;
  keep cases deterministic and distinct (Kobber's startup sound is unison 2x, Mono Legato: set what differs).

## Which build

| Preset | Configuration | Use for |
| --- | --- | --- |
| `dev` | Debug | Debugging; `jassert`; reference renders; compat; the T2/T3 suite |
| `dev-opt` | RelWithDebInfo (`NDEBUG`) | Iteration, slow tests, hidden measurement tags and sweeps: 2–8x faster than Debug, about 5x typically |
| `release`, `audio-lab-release` | Release | Performance, CPU and timing evidence only; tests disabled |

`dev-opt` defines `NDEBUG`, so **`jassert` is compiled out**: a change that relies on an assertion (a new
invariant, a threading assertion, an index check) must also run in Debug before it is reported, at least through T2.
`dev-opt` test presets exclude the reference renders, which are only valid against the Debug captures (Rav's
nonlinear chains amplify optimised rounding to about -57 dBFS at 16x FIR).

Serialize builds that share a build tree; separate presets can build concurrently.

## Writing cheap tests

The suite is CPU-bound, not latency-bound (3 October 2026: the Debug suite is 560 CPU-seconds across 10 cores and
86 s wall; the longest test takes 22.7 s, the `[slow]` tests 340 CPU-seconds, 151 of them Audio Lab). Every second
of test CPU therefore costs wall time in T2 and T3, and splitting a long test helps only if the parts become
cheaper. Make tests cheap at the source:

- **Test at the lowest level that shows the behaviour.** A filter's response is a filter test, not a processor
  render through oscillators, envelopes and oversampling. Use the processor only for wiring, state, threading and
  host-facing behaviour.
- **Render at the lowest rate that exercises the behaviour.** 44.1 kHz is the default host rate for new tests. Be
  realistic about the saving: 44.1 kHz runs about 8 % fewer samples than 48 kHz. Rate is rarely the cost; duration,
  the number of rate and quality points, the oversampling factor and the build configuration are. Existing tests and
  reference renders keep their rates (changing a reference rate changes a baseline).
- **Cover the extremes, not the grid.** A rate sweep covers the lowest internal rate, one standard host rate and
  the highest internal rate unless the behaviour differs in between. Use the highest oversampling factor only when
  the behaviour under test depends on it. Aliasing and Nyquist behaviour depend on the ratio of frequency to rate:
  raise the test frequency rather than the rate.
- **Size durations from the physics.** Settling time from decay time constants or cutoff periods with a stated
  margin; analysis windows of whole periods for a coherent projection; not round seconds.
- **Remove what delays the condition under test:** a startup preset's LFO delay and fade, Drift, or a Free LFO's
  processor clock in a voice-level test (use Retrigger with a start phase).
- **Prove the test discriminates.** Check that it fails without the change (temporarily revert or patch the code),
  and keep the margin between the two outcomes visible in the comment.
- **Tag the cost.** Audition renders and measurement sweeps behind hidden `[.]`; a necessary long assertion behind
  `[slow]`, run in `dev-opt`. Every case carries an owner tag.

### Budgets

`scripts/test.sh` writes a JUnit report and runs `scripts/check-test-budget.sh`: in a parallel `dev` run an always-run
test must finish within 3 s and a `[slow]` one within 60 s, unless `tests/test-time-budget.txt` allows more with a
reason. Parallel load slows each test by up to 3x, so these budgets are tight on purpose. T2 checks the `[slow]` tests
against the same budget in `dev-opt`, where they run faster. An overrun of a second or so on tests near their limit,
with every test passing, can be load: rerun once before acting, and report it (3 October 2026: one T2 run flagged five
tests by up to 1.4 s; the rerun passed). A repeated overrun is a real one.

For Release timing gates, screen with a 10 s run before the required 30 s runs, and run them alone: parallel work
invalidates timing.

## Using the cores

- `ctest` runs with one job per core (`scripts/test.sh` passes `hw.ncpu`; the test presets use 10 jobs).
- Ninja builds in parallel by default; build only the targets needed (`--target vekt_dsp_tests`, one wrapper).
- `scripts/pluginval-dev.sh` validates the three products in parallel against snapshot copies of their bundles
  (5 s for all three once the bundles are built).
- `scripts/lint-changed.sh` runs clang-tidy with 8 jobs.
- Do not run timing gates or Release performance measurements alongside other work.

## Build accelerators

- **ccache** is used automatically when installed (`VEKT_USE_CCACHE`, on by default, off in the `release` preset),
  through a launcher configured from `cmake/ccache-launcher.sh.in` with the `ccache` CMake found. It sets the
  sloppiness that lets translation units built against precompiled headers be cached; that includes ignoring
  `__DATE__` and `__TIME__`, hence no ccache for release builds. A warm rebuild of the test binary after a clean takes
  about 3 s.
- **Precompiled headers** for `vekt_dsp_tests` (JUCE audio processors, DSP, GUI basics and Catch2) cut its compile
  CPU by 28 % (3 October 2026). Add a header to the list only if it is stable and included by most test files.
- **Header hygiene:** every header is parsed by every includer. Keep heavy code in `.cpp` files, include what you
  use, and avoid adding JUCE module includes to widely included framework headers.
- **Clean builds** clear the cache benefit of the build tree and are rarely needed: reconfigure instead, and ask
  before clearing build outputs or caches.

## Lint

clang-tidy (`.clang-tidy`) runs through pre-commit on staged framework and plugin sources (`.pre-commit-config.yaml`,
clang-tidy 21.1 pinned through cpp-linter-hooks, about 8 s per file). It reads the dev build's compile database, so
`cmake --preset dev` must have run. Because the commit hook sees only staged sources, `scripts/lint-changed.sh` (T2)
also lints every framework and plugin source that depends on a changed header, using the dependencies Ninja recorded
in `build/dev`, and checks newly added files against `.clang-format` (report only). Findings are fixed, not
suppressed; a suppression (`// NOLINT(<check>)`) needs a comment giving the reason.

`bugprone-pointer-arithmetic-on-polymorphic-object` is disabled because clang-tidy 21 and 22 crash evaluating it on
JUCE's `juce_Ranges.h`; the other exclusions in `.clang-tidy` are choices. Tests are not linted because they are built
against the test binary's precompiled header, and tools because they are not in the `dev` compile database.

The hook passes Xcode's clang 21 resource directory
(`/Applications/Xcode.app/.../usr/lib/clang/21`) so that the pinned clang-tidy can parse the SDK's libc++. If Xcode
moves or changes its clang version, commits touching framework or plugin sources fail with missing builtin headers
(`NAN`, `INFINITY`): set the path in `.pre-commit-config.yaml` to the output of `xcrun clang -print-resource-dir`, and
keep the clang-tidy pin (`--version`) at that major version.

Warnings are errors in every target compiled from Vekt sources (`VEKT_WARNINGS_AS_ERRORS`, on in `dev`, `dev-opt`
and `audio-lab`), including the plugins' entry points; JUCE's module sources and Catch2 are excluded. JUCE headers
are not system headers, so JUCE inline code included by Vekt sources is subject to the same flags. The linker's
"ignoring duplicate libraries" note is not a compiler warning and is unaffected.

## Measurements (3 October 2026)

Apple Silicon, 10 cores. Times are wall-clock unless noted and swing with load; re-measure before relying on them.

| Check | Time |
| --- | --- |
| Full Debug suite (T3), 501 tests | 79.8–88.3 s |
| `scripts/test.sh --t2` (456 Debug, then 45 slow optimised) | 59.2–60.2 s |
| `scripts/test-affected.sh` for a Kobber change | 30.3 s (347 tests, plus 12 compat in Debug) |
| `scripts/test-affected.sh` for a Rav change | 12.0 s (241 tests, plus 12 compat in Debug) |
| `scripts/pluginval-dev.sh` (three products, parallel, bundles already built) | 4.8 s |
| clang-tidy on every framework and plugin source | 28–31 s |
| `scripts/lint-changed.sh` on 10 affected sources | 11–13 s |
| Test binary rebuild from a warm ccache | 2.6 s |

The plan to split always-run tests over 10 s was dropped after measurement: the suite is CPU-bound, so splitting
would not shorten T3. T2 (slow tests optimised) was added instead.
