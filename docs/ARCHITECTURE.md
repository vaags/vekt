# Architecture

## Dependency Direction

Dependencies point toward small reusable modules. These are the CMake targets
(`framework/<module>`), each linking only what it lists:

```text
plugins (Rav, Glimmer, Kobber, Flint) -> plugin_support, preset_ui, ui, presets, state, dsp
plugin_support                      -> presets, state, dsp
preset_ui                           -> presets, ui
ui                                  -> dsp
presets, state, dsp                 -> JUCE only
audio_analysis                      -> tests and the Audio Lab only, never a plugin
```

Every module also uses the pinned JUCE headers; the Audio Lab tools and tests
link the products' Core libraries.

`vekt_dsp` never includes plugin, editor, preset, or product-specific headers.
Plugin processors are composition roots: they connect parameters, reusable DSP,
state, and format wrappers but do not contain signal-processing algorithms.
Kobber's processor, the largest, composes separately tested parts in its
`Source/`: `KobberSettingsSnapshot` (the one mapping from parameters to voice
settings, shared with tests), `KobberVoiceAllocator` (polyphonic reuse and
stealing, the monophonic modes, note priority, held-key return and the sustain
pedal, unit-tested against a recording fake voice) and `KobberRenderPlan` (how a
segment's sounding voices are grouped into summing units and thread jobs).
`KobberVoice` keeps its per-sample path inline in its header; setup, note start
and release, and diagnostics live in `KobberVoice.cpp`.

New abstractions must be justified by at least two concrete consumers or by a
hard ownership boundary. A generic runtime effect graph is outside version 1.

## Plugin Support

`vekt::plugin_support` holds host integration that every product needs (ADR
0010). Processors own its components and forward the host's calls to them:

- `PresetHost` owns the preset catalog (factory presets plus the user folder),
  the `PresetSession`, project state through `StateManager`, the host program
  API and previous/next navigation. Products supply their preset descriptor,
  sound adapter and extra metadata (Rav's stage order).
- `QualitySelection` creates the shared Tracking and Offline oversampling
  parameters, turns them into the quality the audio thread activates at the
  next block, and publishes the active quality for the editor (all three
  products; Kobber also re-prepares its voices on a change).
- `requireParameter` resolves a parameter's value once, so no block looks a
  parameter up by name. It returns a reference; an identifier the state does
  not hold stops in every build.
- `ChoiceTable` lists a choice parameter's values in choice order with their
  names. Each product keeps its tables in a uniquely named `Source/` header
  (`RavParameterChoices.h`, `GlimmerParameterChoices.h`,
  `KobberParameterChoices.h`; Flint's Mode table sits with its models in
  `Models.h`, Drive Type with its enum in `FlintParameters.h`); the layout builds the parameter's choices from the
  table and the processor and editor decode the value with it, rounded and
  clamped to a valid choice, so names, order and meaning cannot drift apart.
  The shared oversampling lists stay in `dsp/OversamplingChoices.h`, where a
  test proves each name matches the quality it selects.

Product headers live in `include/vekt/<product>/` (processor, parameters,
editor, factory presets) and are included by that path; a product's `Source/`
holds only uniquely named internals.

## Shared Editor Controls

The products consume the same `vekt::ui` controls:

- `ModeButton` owns selection painting and optional drag/drop gestures. RAV
  enables reordering and binds individual stage toggles; Glimmer uses a radio
  group bound to its single model parameter. Parameter and signal-path ownership
  remain in each product.
- `PresetNavigation` owns the preset-name/modified display, previous/next arrows,
  accessible names and focus restoration. Callbacks connect it to the product's
  existing preset session and shared browser; it performs no storage operations.
- `QualitySettings` is the anchored Settings pop-over with the shared Tracking
  and Offline choices, bound through the APVTS to the product's two quality
  parameters; the product places its toggle and sets its bounds (Rav, Kobber).
- `addChoiceItems` fills a combo box from its choice parameter's own list;
  every parameter menu uses it, so no editor keeps a copy of a choice list. An
  identifier that names no choice parameter stops in every build.
- `UndoRedoControls` owns history buttons, availability and action tooltips.
  Products supply their UndoManager, a pre-action APVTS flush, and a post-action
  UI refresh. Pending parameter edits are also flushed before a new preset-load
  transaction so rapid loads preserve a consistent undo/redo baseline.

- `Oscilloscope` draws a processor-owned `vekt::dsp::ScopeTap` (wait-free
  stereo ring the audio thread fills after output gain) and refreshes itself
  per display frame via `VBlankAttachment`, skipping repaints while nothing
  new or only silence arrives. Each trace is filled as a band between the
  per-pixel-column lowest and highest samples, which costs a third to a half
  of stroking the same line. `findScopeTrigger` (in `ScopeTap.h`) picks the
  newest rising zero crossing with hysteresis, so the trigger is testable
  without a display.

- `RotaryControl::setModulation` shows where modulation can take a knob as a
  `ModulationRing`: a thin arc in its own lane outside the value ring, mapped
  through the slider's range and skew (wrapped on endless controls; past
  either end of a bounded travel it draws an overflow mark). Products supply
  the range in parameter units, already limited to what the processor reaches;
  the pointer and readout keep the base value. Kobber derives ranges and limits
  from `LfoDestinations.h`, the same table its processor uses to scale LFO
  depths, and cutoff limits from `FilterLimits.h`, which the voice uses too, so
  display and sound cannot drift.
  The live dot follows the processor's published LFO outputs through
  `vekt::dsp::DisplayHistory` (wait-free, one frame per block) and
  `DisplayTimeline`, which shows them a short, self-adjusting delay back so
  motion stays smooth whatever the host's block size; see [UI_UX.md](UI_UX.md)
  for its behaviour and measurements.
- The timeline lives in the framework with Kobber as its only consumer, an
  exception to the two-consumer rule: it is part of the reusable modulation
  display (any product animating a live dot needs it), built for reuse from the
  start.

These controls are independent of product IDs and DSP. Both editors keep their
own composition/layout and refresh the shared controls on their existing UI timer.

## Real-Time Contract

Code reachable from `processBlock` must not:

- allocate or release heap memory;
- acquire locks;
- perform file, network, console, or logging I/O;
- throw or catch exceptions;
- mutate `ValueTree` state;
- execute work with input-dependent unbounded duration.

Buffers and filter paths are allocated during `prepareToPlay`. Parameters are
read through cached atomics and continuous values are smoothed. UI meters use
atomic scalar publication.

A requested oversampling quality applies at the start of the next audio block,
during playback too (ADR 0010). Every quality path is prepared in advance, so
activation only switches to it, resets the affected state and updates dry and
reported latency; the format wrappers notify the host on the message thread.
The audio may drop or click at the switch; no smooth transition is required.

## DSP Contracts

- Effects (Rav, Glimmer) process stereo input to stereo output in version 1;
  instruments (Kobber, Flint) take MIDI and produce stereo output.
- Every product offers the same Tracking and Offline oversampling choices
  (`vekt/dsp/OversamplingChoices.h`, ADR 0001): Off, 2x/4x minimum-phase IIR and
  2x/4x/8x/16x linear-phase FIR. Defaults are per product: Rav and Glimmer track
  at 4x IIR and render offline at 16x FIR; Kobber tracks Off and renders at 4x FIR;
  Flint tracks and renders Off, so a bounce matches playback.
- Per-sample recursions must stay correct at the highest internal rate,
  `vekt::dsp::maximumInternalSampleRate` (192 kHz x16). A state that steps toward
  a target (envelope, glide, smoothing, drift) or accumulates a phase or a time
  (oscillator, LFO fade) keeps its state, coefficients and rate in `double`, or
  tracks the remaining distance so it converges; in single precision the steps
  fall below rounding there and the state stalls or drifts. Linear parameter
  ramps use `vekt::dsp::LinearRamp` (double), as `ControlTransition`,
  `AdaptiveAutoGain`, `MatchedToneStage` and `TanhStage` do inside. Tests of such state
  run at that constant rather than a hard-coded rate. Audio-rate filters whose
  state follows the signal may stay in `float` when measured below the
  reference-render tolerance.
- Every path uses integer latency so host reporting and dry alignment agree.
- Effects only (Rav, Glimmer):
  - Dry/wet interpolation is linear.
  - Input gain precedes the dry/wet split; output gain follows the mix.
  - Mix at 0% preserves the post-input-gain dry signal after active wet latency.
  - Host bypass preserves raw input after reported plugin latency.

Product signal paths and their contracts belong to the product documents:
[Rav](RAV_VALIDATION.md), [Glimmer](GLIMMER_VALIDATION.md) (its `RotaryEngine`,
model switching and latency), [Kobber](KOBBER_VALIDATION.md) and
[Flint](FLINT_VALIDATION.md) (its engine host and model sheets, ADR 0011); Kobber's
quality scope is recorded in ADR 0001.

## Compatibility

Published parameter IDs, manufacturer codes, plugin codes, and bundle IDs are
immutable. Parameter IDs use JUCE version hints. Project and preset state use
separate versioned schemas, both JSON documents the products own rather than JUCE
formats: host project state is a `vekt.project` document (parameters by ID,
project metadata including the preset selection; see `StateManager.h`), so it can
be read without JUCE. Products with a published compatibility boundary use
explicit migrations for supported older schemas. A product that explicitly remains
pre-release may instead reject obsolete schemas while breaking changes are expected;
that policy must be documented by the product and loads must remain all-or-nothing.

All products are pre-release (1 October 2026): no older project or preset
format is supported. Factory presets are stored at each product's current sound
schema, and there are no project or preset sound migrations. Parameters missing
from a project take their defaults (APVTS). A restore leaves every boolean at its
exact saved or default value even after fractional VST3 automation; `StateManager`
owns this, so products add no per-parameter fixups (2 October 2026). The frozen state, parameter and
audio fixtures in `tests/fixtures` (see its README) start the compatibility
record; they become binding at the first release.

Quality settings and editor geometry belong to project state, not sound presets.
Host automation owns its history; local undo covers UI gestures and preset loads.

Preset documents are UTF-8 JSON with product and schema identity. Each
product supplies an explicit sound-parameter allowlist; missing, unknown,
non-numeric, and out-of-range values reject the entire load before mutation.
Repositories only perform message-thread file I/O and receive their storage root
from the product; the reusable layer does not infer host sandbox policy.
`PresetCatalog` combines factory documents with an optional user repository.
`PresetSession` owns loaded identity, comparison snapshots and common workflows;
products provide capture/validate/apply/migrate/match adapters. `vekt::preset_ui`
provides the shared browser without product-header dependencies. ADR 0003
specifies the global v2 format and the initial implementation's limitations.
Factory presets are compiled into each product, decoded through the public JSON
codec, and exposed as an immutable bank through the host program API. The
preset selection is persisted in project metadata as the preset session's
`vektPresetSelection`; the host program is the selected factory preset, or 0
when a user preset or no preset is selected (ADR 0010). User presets remain a
separate mutable editor-facing source, are listed in natural sort order, and
cannot shadow a case-insensitively matching factory name.
Standalone, VST3, and AUv2 user presets resolve beneath
`~/Library/Audio/Presets/Thomas Vaags/<product name>` (`Rav`, `Glimmer`,
`Kobber` or `Flint`). File-access failures must remain visible; a shared desktop path
does not establish sandbox access in every host. Validate save/load and native
choosers in actual hosts before making that claim.

## Validation

Each DSP primitive receives focused tests before integration. The release gates
will additionally include allocation checks, FFT alias measurements, latency and
null tests, state migration tests, pluginval strictness 10, `auval`, and host
smoke tests for VST3 and AUv2.

Shared editor behavior and validation criteria are defined in
[`UI_UX.md`](UI_UX.md); preset-specific interaction rules are defined in
[`PRESET_UX.md`](PRESET_UX.md).
