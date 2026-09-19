# Architecture

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

## Compatibility

Published parameter IDs, manufacturer codes, plugin codes, and bundle IDs are
immutable. Parameter IDs use JUCE version hints. Project and preset state use
separate versioned schemas with explicit migrations.

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
