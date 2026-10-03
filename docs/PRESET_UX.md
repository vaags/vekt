# Preset UX

Preset interactions must follow these rules across Vekt products.

The reusable implementation and current follow-up boundaries are described in
`decisions/0003-framework-preset-browser.md`. The current browser opens from the
preset-name button and provides folder scope, text search, all-tags filtering,
Save As, confirmed replacement/deletion, tag and description editing, preset moves and file
import/export. The first version uses a folder selector and comma-separated tag
fields. Import adds to the library without loading; export writes the selected
stored preset, whereas Save As captures the live sound.

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

## Descriptions

- A preset may carry a `description`: one line of plain text, at most 280
  characters, saying what the sound is and where it fits. Documents without one
  stay valid; an empty description is not written.
- The browser shows the selected preset's description, includes it in text
  search, and lets Save As and "Update details" set it on user presets. The
  preset-name button's tooltip shows the loaded preset's description.

## Saving and deletion

- Saving uses create-only behavior by default. If a name already exists, ask
  the user to confirm replacement and retry with explicit replace mode.
- Treat names as case-insensitively unique within each folder. Equal user names
  in different folders are allowed; factory names remain reserved.
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

- VST3, Standalone, and AUv2 use `~/Library/Audio/Presets/Thomas Vaags/<product name>`:
  `Rav`, `Glimmer`, or `Kobber`. Each product has a separate library.
- Actual host sandbox access is a manual gate. Report file-access errors rather
  than silently selecting another storage location or claiming sandbox safety.
- File chooser import/export is message-thread-only. Validate imported content
  completely before changing parameters or writing into the user repository.

## Factory preset authoring

- Factory presets are immutable resources embedded in each plugin. Add a valid
  `.vektpreset` beneath that plugin's `Resources/Presets` directory; its parent
  directory becomes the factory folder and it appears after the next build and
  launch. Every factory preset carries a description; each product's factory
  preset test enforces this.
- Existing host-program positions are locked by `factory-order.lock` and
  ordered by `factory-order.txt`. The released order must remain its exact
  prefix; removing, reordering, or inserting before a released entry is a
  configure error. New files are appended after those entries in deterministic
  path order; promote them to both files when their program position ships.
- User presets remain disk-backed, writable, and visibly distinct from factory
  presets in the browser.
