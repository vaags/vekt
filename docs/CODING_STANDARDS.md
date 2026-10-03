# Coding Standards

How Vekt code is written. [ARCHITECTURE.md](ARCHITECTURE.md) owns what the code must do (dependency direction, the
real-time contract, DSP contracts including per-sample precision, compatibility); where the two overlap, it wins and
this document links to it. [DEVELOPMENT_WORKFLOW.md](DEVELOPMENT_WORKFLOW.md) owns process and
[VERIFICATION_SPEED.md](VERIFICATION_SPEED.md) how checks are run. Sources: the C++ Core Guidelines, JUCE's coding
standards and documentation, and established real-time audio practice (audio-thread rules, lock-free publication),
adapted to this codebase. When existing code disagrees with this document, follow this document in code you write
and leave unrelated code alone.

Tooling enforces part of this: warnings are errors in Vekt targets (`VEKT_WARNINGS_AS_ERRORS`, on in the development
presets), clang-tidy (`.clang-tidy`) runs on every commit and at T2, and `.clang-format` describes the layout (report
only; existing files are not reformatted).

## General

- **Clarity over cleverness.** Code is read far more than written. Name things for what they mean to a reader of
  the product (`cutoffOctaves`, not `co`); prefer a plain loop to a dense algorithm chain when the loop is clearer.
- **Smallest correct change.** Touch what the task needs. No drive-by refactors, renames or reformatting of
  unrelated code; surface them instead.
- **Comments explain why**, not what: constraints, units, invariants, measured reasons ("in float the step falls
  below rounding at 3 MHz"). No commented-out code, no change history (that is git's), no dates in code; dated
  evidence lives in the validation documents and ADRs.
- **One owner per fact.** A rule, constant, mapping or list lives in one place and is referenced (quality choice
  lists in `dsp/OversamplingChoices.h`, the preset schema in `presets/PresetSchema`). Duplicated knowledge drifts.
- **Abstractions need two consumers** or a hard ownership boundary (the project rule in CLAUDE.md), not
  anticipated reuse. Product-specific code stays in its plugin.
- **Make invalid states unrepresentable** where cheap: `enum class` over flags, `std::optional` over sentinels,
  types with invariants enforced in one constructor.
- **Fail loudly in development, safely in production.** `jassert` for programmer errors, `juce::Result` for
  expected failures, never silent clamping of invalid data (state and presets are all-or-nothing). One exception:
  static wiring errors that construction tests always catch, such as a parameter ID the state does not hold
  (`requireParameter`, `ui::addChoiceItems`), stop in every build instead of leaving a null for later. These abort
  paths cannot be tested in-process.
- **Every change is tested** at the cheapest level that proves it, and a new test is shown to fail without the
  change (VERIFICATION_SPEED.md).

## C++20

- **Ownership:** values and members first; `std::unique_ptr` for owned heap objects, created with
  `std::make_unique`; raw pointers and references only as non-owning views. No `new`/`delete` outside a factory
  that returns ownership immediately. `std::shared_ptr` only for genuinely shared lifetime (rare here).
- **Rule of zero:** let members manage resources; write special members only when a class owns a resource
  directly, and then all five (or `= delete` them). `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` on JUCE-style
  classes that must not copy.
- **Const and constexpr:** `const` by default for locals, parameters taken by value that are not moved, and member
  functions that do not mutate; `constexpr` for compile-time constants and pure helpers; `inline constexpr` for
  header constants.
- **Interfaces:** `[[nodiscard]]` on functions whose result must be used (queries, `Result`, factories);
  `noexcept` on everything reachable from the audio thread and on moves; `explicit` single-argument constructors;
  `override` (or `final`) on every override, never `virtual` on an override.
- **Parameters:** cheap types (scalars, `std::span`, `std::string_view`, small trivially copyable structs) by
  value; others by `const&`; take by value only to move into storage (sink parameters), and then move.
- **Types and conversions:** `enum class`; no C-style or functional casts (`static_cast`, `std::bit_cast`); no
  implicit narrowing or float/int conversions (the warnings are errors); `std::size_t` for sizes and indices into
  standard containers, widened before arithmetic (`2 * static_cast<std::size_t>(i)`, not
  `static_cast<std::size_t>(2 * i)`).
- **Floating point:** compare with a tolerance, or with `juce::exactlyEqual` where exactness is the point
  (determinism tests, snapped values); `-Wfloat-equal` is on, also inside `REQUIRE`/`CHECK` (Catch2's decomposition
  does not hide it). `double` for per-sample state that steps toward a
  target or accumulates (ARCHITECTURE.md, DSP Contracts); `float` audio buffers.
- **Containers and views:** `std::array` for fixed sizes, `std::vector` sized in `prepare`, `std::span` to pass
  ranges; no allocation on the audio thread (ARCHITECTURE.md, Real-Time Contract).
- **Optionals:** check before access, or bind to a local and check that (clang-tidy cannot follow a check through
  an intervening member call or a nested optional).
- **Headers:** `#pragma once`; include what you use; product headers by their qualified path
  (`<vekt/<product>/...>`), internal `Source/` headers by unique name; no relative `../../` includes. Templates and
  tiny inline functions in headers, other code in `.cpp` files, since every header is parsed by every includer
  (VERIFICATION_SPEED.md).
- **Language level:** C++20 only (`std::expected` and other C++23 features are not available).

## JUCE

- **Parameters:** APVTS with `juce::ParameterID` and an explicit version hint; parameter IDs are immutable once
  published. Audio code reads parameters through values resolved once (`plugin_support::requireParameter`),
  never by name in `processBlock`. A choice parameter is built and decoded through one `plugin_support::ChoiceTable`
  (names and values together, decoded to an enum or typed value, never compared as a bare index), and its menu is
  filled from the parameter (`ui::addChoiceItems`).
- **Threads:** the audio thread runs `processBlock` only. Message-thread work (files, presets, editor) asserts it
  where misuse is plausible. Audio-to-UI publication through atomics or the wait-free taps (`ScopeTap`,
  `DisplayHistory`); UI-to-audio through parameters or atomics. Never `juce::AsyncUpdater::triggerAsyncUpdate`,
  `juce::MessageManager` calls, `juce::String` construction, `DBG` or logging from the audio thread.
- **Smoothing:** `vekt::dsp::LinearRamp` or `ControlTransition` (double internally), prepared at the rate they run
  at; never a `juce::SmoothedValue<float>` at an oversampled rate.
- **Components:** child components are members, added with `addAndMakeVisible`/`addChildComponent`; layout in
  `resized()` from logical bounds; no painting from mutable processor containers; `juce::Component::SafePointer` in
  any asynchronous callback; `setLookAndFeel(nullptr)` in the destructor of a component that set one, with the
  LookAndFeel declared before the components that use it.
- **Attachments:** declare attachments after the controls they attach so they are destroyed first.
- **Strings and results:** `juce::String` at JUCE API boundaries (UI text, ValueTree, file paths); `juce::Result`
  for fallible message-thread operations.
- **Assertions:** `jassert`/`jassertfalse`, not `assert`. They compile out of `dev-opt` and Release, so behaviour
  must not depend on them and tests must check what matters (VERIFICATION_SPEED.md: Debug at milestones).
- **Leak detection:** keep JUCE's leak detector on in Debug; a leak report is a defect.

## Tests

- Catch2 v3, one behaviour per `TEST_CASE`, named as a sentence stating the expected behaviour.
- Every test case carries an owner tag (`[rav]`, `[glimmer]`, `[mono]`, or a framework module such as `[dsp]`,
  `[ui]`, `[presets]`, `[plugin-support]`, `[compat]`, `[audio-lab]`), and a case named after a product carries that
  product's tag; `Every test case carries a product or framework tag` enforces it. Long cases add `[slow]`;
  measurements, renders and fixture captures are hidden (`[.]` plus their own tag) and carry no owner tag, since
  naming a tag runs the hidden cases that carry it.
- Assertions state the physical criterion and its margin in a comment where the number is not obvious.
- Cost and rates: VERIFICATION_SPEED.md.

## Layout

- Tabs for indentation (width 4), Allman braces, 120 columns, `camelCase` functions and variables, `PascalCase`
  types, no member prefixes or suffixes: the prevailing style, described in `.clang-format` and `.editorconfig`. New
  files follow it (`scripts/lint-changed.sh` checks them); in an existing file follow the file's own style, including
  the few space-indented files, where an editor following `.editorconfig` would insert tabs. A one-time reformat of everything would change 7,295 of 43,577 lines (17 %, 3 October
  2026) and has not been done.

## Decisions where practices diverge

| Topic | Common practice | Vekt | Why |
| --- | --- | --- | --- |
| Containers | JUCE `Array`/`OwnedArray` (JUCE style) or `std::` (Core Guidelines) | `std::` in DSP and framework; JUCE types only at JUCE API boundaries | Standard containers are value types with known complexity and no allocation surprises; JUCE APIs take their own types |
| Errors | Exceptions (Core Guidelines) | No exceptions on audio paths; `juce::Result` elsewhere | Real-time contract; JUCE convention; `std::expected` is C++23 |
| Assertions | `assert` | `jassert` | Breaks into the debugger in hosts; JUCE convention |
| Naming and braces | Various | JUCE-like `camelCase`, Allman, tabs | The codebase's prevailing style; consistency beats preference |
| Reuse | "Don't repeat yourself" | Two concrete consumers first | Premature shared abstractions couple products (ARCHITECTURE.md) |
| Precision | `float` audio everywhere | `float` buffers, `double` slow per-sample state | Single precision stalls at high internal rates (ARCHITECTURE.md) |
| Warnings | Warnings as advice | Errors in Vekt sources; JUCE module sources and Catch2 excluded | Standards erode without enforcement; we cannot fix third-party code |
| Formatting | Enforced formatter | Report-only `.clang-format`, new files must follow | A mass reformat would bury history and in-flight work |
