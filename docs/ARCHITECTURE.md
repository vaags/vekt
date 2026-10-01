# Architecture

Mono quality scope as of 27 September 2026: selectable and planned modes end
at 8x (1x/2x/4x/8x). Historical 16x/32x offline references remain diagnostic,
not selectable quality modes. Stored 16x project states are rejected, not
silently mapped to 8x.

## Dependency Direction

Dependencies point toward small reusable modules:

```text
plugins
  -> plugin_support
  -> ui
  -> presets
  -> state
  -> dsp
```

`vekt_dsp` never includes plugin, editor, preset, or product-specific headers.
Plugin processors are composition roots: they connect parameters, reusable DSP,
state, and format wrappers but do not contain signal-processing algorithms.

New abstractions must be justified by at least two concrete consumers or by a
hard ownership boundary. A generic runtime effect graph is outside version 1.

## Shared Editor Controls

RAV and Glimmer consume the same `vekt::ui` controls:

- `ModeButton` owns selection painting and optional drag/drop gestures. RAV
  enables reordering and binds individual stage toggles; Glimmer uses a radio
  group bound to its single model parameter. Parameter and signal-path ownership
  remain in each product.
- `PresetNavigation` owns the preset-name/modified display, previous/next arrows,
  accessible names and focus restoration. Callbacks connect it to the product's
  existing preset session and shared browser; it performs no storage operations.
- `UndoRedoControls` owns history buttons, availability and action tooltips.
  Products supply their UndoManager, a pre-action APVTS flush, and a post-action
  UI refresh. Pending parameter edits are also flushed before a new preset-load
  transaction so rapid loads preserve a consistent undo/redo baseline.

- `RotaryControl::setModulation` shows where modulation can take a knob as a
  `ModulationRing`: a thin arc in its own lane outside the value ring, mapped
  through the slider's range and skew (wrapped on endless controls; past
  either end of a bounded travel it draws an overflow mark). Products supply
  the range in parameter units, already limited to what the processor reaches;
  the pointer and readout keep the base value. Mono derives ranges and limits
  from `LfoDestinations.h`, the same table its processor uses to scale LFO
  depths, and cutoff limits from `FilterLimits.h`, which the voice uses too, so
  display and sound cannot drift.
  The live dot is computed in the editor from the processor's published LFO
  outputs, a sounding-voice flag and effective rates, and is refreshed per
  display frame by a `VBlankAttachment`. The outputs travel through
  `vekt::dsp::DisplayHistory` (wait-free, one frame per block stamped with its
  sample position) and `DisplayTimeline`, which shows them a short,
  self-adjusting delay back, interpolated between blocks, so motion stays
  smooth whatever the host's block size, burst pattern or display refresh
  rate. The delay covers the longest recent publishing gap (capped at 250 ms)
  and changes gradually. Hosts that render ahead of playback make the display
  lead the sound by that amount; no plugin-side clock can see it.
- The timeline lives in the framework with Mono as its only consumer, an
  exception to the two-consumer rule: it is part of the reusable modulation
  display (any product animating a live dot needs it), which was built for
  reuse from the start. Its requirement was measured in a simulated 60 fps
  display: showing each block's latest value instead, frame-to-frame steps
  vary by 32 % at 512-sample blocks, and at 1024 samples 22 % of frames freeze
  (61 % at 2048 or with bursty hosts, with steps up to 2.6x); with the
  timeline they vary by under 1.5 % and never freeze.
- The dot's handover to the blur band assumes 60 drawn frames per second, what
  JUCE delivers on macOS even on 120 Hz displays. Where frames are drawn faster
  the handover is conservative (the dot gives way sooner than it must), not
  wrong.

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

Oversampling quality activation is not an audio-thread operation. The plugin
support layer must defer requested changes while transport is known to be
running, suspend processing on the message thread, activate and reset the
prepared path, update dry latency and reported plugin latency, notify the host,
and then resume processing.

## DSP Contracts

- Processing uses stereo input and output in version 1.
- Oversampling choices are Off, 2x, and 4x with minimum- or linear-phase filters.
- The default is maximum-quality 4x minimum phase.
- Every path uses integer latency so host reporting and dry alignment agree.
- Dry/wet interpolation is linear.
- Input gain precedes the dry/wet split; output gain follows the mix.
- Mix at 0% preserves the post-input-gain dry signal after active wet latency.
- Host bypass preserves raw input after reported plugin latency.

## Glimmer Engine

Glimmer's product-local `RotaryEngine` owns cabinet profiles, tone/directivity
filters and microphone geometry. Classic/Wide use separate horn and drum paths;
Drum routes the full input through one rotating speaker. Model switching uses
two preallocated instances with warmup and a linear crossfade, not duplicated
algorithms. Raw stereo information is retained until an explicit wet-only Width
transform. Only the preamp is oversampled, and Auto Gain acts before rotary
modulation. Dry and bypass align to the fixed 8 ms pickup center plus active
oversampling latency, independently of delay-buffer capacity. Intentional travel
modulation and cabinet filter phase are not compensated away. See
[GLIMMER_VALIDATION.md](GLIMMER_VALIDATION.md) for the signal and state contracts.

## Compatibility

Published parameter IDs, manufacturer codes, plugin codes, and bundle IDs are
immutable. Parameter IDs use JUCE version hints. Project and preset state use
separate versioned schemas. Products with a published compatibility boundary use
explicit migrations for supported older schemas. A product that explicitly remains
pre-release may instead reject obsolete schemas while breaking changes are expected;
that policy must be documented by the product and loads must remain all-or-nothing.

Quality settings and editor geometry belong to project state, not sound presets.
Host automation owns its history; local undo covers UI gestures and preset loads.

Preset documents are UTF-8 JSON with product and schema identity. Each
product supplies an explicit sound-parameter allowlist; missing, unknown,
non-numeric, and out-of-range values reject the entire load before mutation.
Repositories only perform message-thread file I/O and receive their storage root
from the product, allowing VST3 and AUv3 containers to choose appropriate paths.
`PresetCatalog` combines factory documents with an optional user repository.
`PresetSession` owns loaded identity, comparison snapshots and common workflows;
products provide capture/validate/apply/migrate/match adapters. `vekt::preset_ui`
provides the shared browser without product-header dependencies. ADR 0003
specifies the global v2 format and the initial implementation's limitations.
Factory presets are compiled into each product, decoded through the public JSON
codec, and exposed as an immutable bank through the host program API. The
selected factory name is persisted in project metadata. User presets remain a
separate mutable editor-facing source, are listed in natural sort order, and
cannot shadow a case-insensitively matching factory name.
Desktop user presets resolve beneath `~/Library/Audio/Presets/Vekt/Vekt
Rav`. AUv3 callers must provide an app-group identifier; failure to resolve
its container is reported and never falls back to desktop storage.

## Validation

Each DSP primitive receives focused tests before integration. The release gates
will additionally include allocation checks, FFT alias measurements, latency and
null tests, state migration tests, pluginval strictness 10, `auval`, and host
smoke tests for VST3 and AUv3.

Shared editor behavior and validation criteria are defined in
[`UI_UX.md`](UI_UX.md); preset-specific interaction rules are defined in
[`PRESET_UX.md`](PRESET_UX.md).
