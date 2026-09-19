# ADR 0002: Preset Schema and Storage

## Status

Accepted

## Decision

Sound presets use a versioned, human-readable UTF-8 JSON document with explicit
product identity, preset name, parameter values keyed by stable IDs, and
optional metadata. Each product provides an allowlist of sound parameter IDs.
Validation is all-or-nothing before parameters are changed.

Project-only quality settings, host bypass, editor geometry, and current preset
navigation state are excluded. Loading a preset updates APVTS values on the
message thread in one undo transaction.

Storage is abstracted behind `PresetRepository`. `FilePresetRepository` receives
its root directory from the caller and writes through a temporary file before
atomic replacement. Product code selects the standard desktop location or an
AUv3 app-group container; the reusable layer does not infer sandbox policy.
Desktop presets use `~/Library/Audio/Presets/Vekt/Vekt Rav`. AUv3 storage
requires an explicitly configured app-group container and never falls back to
the desktop location. User save, delete, load, and navigation remain
message-thread operations and do not alter the immutable factory host-program
bank.

The global format and its v1-to-v2 migration are now specified in ADR 0003.
Format versions and product sound versions evolve independently. Newly written
v2 documents are not readable by older v1-only plugin builds.

## Consequences

- Presets cannot accidentally alter quality, bypass, or editor state.
- Invalid and partial documents fail without partially changing parameters.
- Factory presets may use the same validated document format without filesystem
  ownership.
- File operations remain outside the audio callback.
