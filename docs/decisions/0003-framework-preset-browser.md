# ADR 0003: Framework preset format and browser

## Status

Accepted; initial synchronous implementation. Background storage and the richer
folder-tree/tag-chip UI remain follow-up work, not implemented guarantees.

## Ownership

`vekt::presets` owns documents, migration of the global envelope, repositories,
catalogs, filtering, storage paths and `PresetSession`. `vekt::preset_ui` owns
the browser and management controls and depends only on framework modules.
Neither module includes Rav headers. Plugins supply `PresetProduct`, factory
documents and `PresetSoundAdapter` callbacks. The session outlives the editor.

The adapter captures, migrates, validates, applies and compares product sound.
Validation and migration must not mutate live sound. Applying sound must be an
all-or-nothing operation with one undo transaction. `PresetSchema` remains the
shared APVTS helper for parameter-only sound schema 1.

## Portable format

The `.vektpreset` UTF-8 JSON format is global, not a Rav format:

- `format`: `vekt.preset`
- `schemaVersion`: global envelope version, currently 2
- `id`: opaque stable identity
- `product`: stable compatible product ID
- `soundSchemaVersion`: independently versioned product payload
- `name`, `tags`, optional `metadata`
- `parameters`: numeric parameter values keyed by stable IDs
- `soundState`: optional structured, product-validated sound information

Folder paths are repository locations and are never encoded in portable files.
Origin is determined by the source, never trusted from an imported file.
Quality, host bypass, browser filters and geometry are not sound preset fields.

Version 1 documents migrate in memory to version 2 with sound version 1.
Legacy category metadata also becomes a tag. Reading does not rewrite files.
Legacy IDs derive from product and location; managed moves preserve them by
persisting that identity. New saves/imports receive UUIDs. Replacement retains
identity. Old plugin builds cannot read newly written version 2 documents.

## Library semantics

Physical folders support nested user organization and duplicate names in
different folders. Names are case-insensitively unique per folder. Factory
names remain reserved. Catalogs are product-scoped; incompatible presets are
not offered. Ambiguous IDs are unavailable rather than resolved arbitrarily.

Search matches name, folder and tags. Folder scopes include descendants. Tags
are trimmed and case-insensitively deduplicated. Selected tags all must match.
Selection is separate from loading. Enter, double-click or Load applies sound;
toolbar navigation continues across the whole catalog.

Tag updates preserve stored sound and do not clear the live sound's modified
state. Import adds a validated copy without applying it. Export writes the
selected stored preset. Save As captures current sound with a new identity.
Replacement and deletion require confirmation in the browser.

Project metadata may store the loaded comparison baseline through
`selectionState`/`restoreSelection`. Restore neither reads the library nor
applies sound; project parameters remain authoritative.

## Current limitations and follow-up gates

- Repository operations and catalog refresh are synchronous, outside audio
  processing. Background scanning, cancellation and immutable publication are
  still required for large libraries.
- The first UI uses a folder selector and comma-separated tag controls, not a
  folder tree, tag suggestions or tag chips.
- Preset moves and empty-folder removal exist at repository level; folder
  rename/move and empty-folder deletion UI remain outstanding.
- Writes use temporary replacement and cooperating-process locks for save and
  delete. Multi-operation moves are not crash-atomic; optimistic stale-write
  detection is not implemented.
- Rav still uses its existing parameter-only sound schema: stage order and
  stage-enable capture/migration/undo must be addressed before claiming complete
  stage-chain sound recall.
- VoiceOver, native chooser sandbox behavior, and AUv3/DAW manual validation
  remain release gates.

Framework regression fixtures use two different product schemas without Rav
headers. Integration coverage retains factory host-program ordering and checks
the open browser's bounds at supported editor sizes.