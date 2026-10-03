# ADR 0011: Flint Engine Architecture

## Status

Accepted (3 October 2026) by Thomas in the Flint Milestone 1 planning. Not yet implemented; the product contract and
plan status are in [FLINT_VALIDATION.md](../FLINT_VALIDATION.md).

## Context

Flint is a percussion synthesizer with one instrument per plugin instance and many synthesis models (31 in V1) grouped
under nine modes. Its parameter layout, model lists and engine boundary freeze at the first release, while models will
keep arriving after it. Hosts store a choice parameter's automation as a fraction of its list length, but Vekt project
state and presets store the choice index (`StateManager`, `PresetSchema`).

## Decision

1. **Parameters per model.** Each model owns its host parameters, `flint.<mode>.<model>.<control>`, four or five per
   model. No generic macro slots whose meaning changes with the model.
2. **Mode and per-mode Model choices, not automatable.** `flint.mode` plus one `flint.<mode>.model` per mode, so each
   mode remembers its model. Mode, Model and Drive Type are not automatable and their lists only grow at their end:
   projects and presets keep their index, and there is no automation for a growing list to break. Model switching
   cannot be automated or mapped to host macros.
3. **One engine host, explicit engines.** A product-local `FlintEngineHost` owns preallocated engines and processes only
   the selected one, per block segment, behind a Flint-only `Engine` interface. No voice allocator, no shared modal base
   class until a second modal model exists, no effect graph.
4. **Absolute pitch, no pitch-follow.** `flint.pitch` (MIDI 12–108) and `flint.fine` (±50 cents) set pitch in every
   model; MIDI note numbers never do. A change glides the ringing object over 15 ms.
5. **Reproducible variation.** Each hit's random values hash the instance seed, the hit's song position and its order on
   that sample; the seed lives in project metadata, not presets.
6. **Hi-Hat Two-Note Mode** (built with Hi-Hat): optional, default off; user-set closed and open notes (GM 42/46), other
   notes play open, a closed strike damps the open ring.
7. **Ensemble per model.** Count and Spread are parameters of each model that supports Ensemble, with exact ranges.
8. **Engine families.** Eight families serve the 31 models: Analog Resonator, Analog Oscillator, Metal Network,
   Noise/Burst, Modal, FM, Particle and a Percussion exciter–resonator family of its own.
9. **Shared quality.** Tracking and Offline quality through `plugin_support::QualitySelection` and the shared choice
   lists; Flint defaults to Off/Off so a bounce matches playback.

## Consequences

- After release, adding a model or mode appends to a frozen choice list. The compat manifest test must then accept a
  choice list whose frozen version is its beginning, for Mode, Model and Drive Type; that test changes with the first
  model added after release.
- Unimplemented entries exist in the lists from the start and play silence until built.
- Host parameter lists grow with each model (about two per model for Ensemble where supported).
- A loop of one bar repeats its random variation exactly on every pass.

## Alternatives considered

- **Generic macro slots:** a short host list, but automation silently changes meaning with the model.
- **One flat Model choice:** simplest state, but no per-mode memory and one long host list.
- **Reserved placeholder choices, automatable:** stable automation, but hosts list empty entries.
- **Pitch-follow with a two-resonator pool:** melodic mallet play, but limited polyphony against the one-object design;
  Thomas chose an automatable absolute pitch instead.
- **Free-running or transport-reset randomness:** renders would depend on where playback started, and identical seeds
  would correlate layered instances.
