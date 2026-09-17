# Preset UX

Preset interactions must follow these rules across Vekt products.

## Selection and modification

- Show factory presets before naturally sorted user presets, with a visible
  section or origin distinction.
- A loaded or newly saved preset is clean. Changing any sound parameter marks
  it modified; render this with a subtle marker beside its name.
- Undo and redo derive modified state from live parameter values rather than
  manually toggling a flag.
- If the selected user preset is deleted, keep the current sound but clear the
  selection. Previous or next then starts from the corresponding list endpoint.
- Host programs remain the immutable factory bank. User-preset navigation must
  not change the host's program count.

## Saving and deletion

- Saving uses create-only behavior by default. If a name already exists, ask
  the user to confirm replacement and retry with explicit replace mode.
- Treat names as case-insensitively unique to match normal macOS filesystems.
- Never allow a user preset to shadow, replace, or delete a factory preset.
- Confirm deletion with the preset name. Do not offer deletion for factories.
- Keep dialogs open after validation or I/O errors and show the actionable
  `juce::Result` message beside the relevant control.

## Navigation and commands

- Previous and next wrap across the combined factory/user list.
- Disable save, delete, and navigation commands while their prerequisites are
  unavailable rather than accepting commands that are guaranteed to fail.
- Provide menu items and keyboard-accessible commands for save, delete,
  previous, next, import, export, undo, and redo. Icon-only buttons require
  tooltips and accessible names.

## Storage and file access

- VST3 and Standalone use `~/Library/Audio/Presets/Vekt/Vekt Saturator`.
- AUv3 requires its configured app-group container and must never fall back to
  desktop storage.
- File chooser import/export is message-thread-only. Validate imported content
  completely before changing parameters or writing into the user repository.
