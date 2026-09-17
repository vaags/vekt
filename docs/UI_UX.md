# UI and UX

This document defines interaction and presentation conventions shared by Vekt
plugin editors. Product-specific layouts may vary, but common behavior must not.
Preset workflows are specified separately in [PRESET_UX.md](PRESET_UX.md).

## Product principles

- Put the sound controls first. Branding, decoration, and explanatory copy must
  not displace the primary editing workflow.
- Prefer direct manipulation, predictable state, and restrained feedback over
  hidden gestures or decorative motion.
- Keep common actions in stable positions across products. A control must not
  move when its value, label, validation state, or hover state changes.
- Use progressive disclosure for setup and quality options. Advanced controls
  remain discoverable without competing with routine sound shaping.
- Never imply that a requested state is active until the processor confirms it.

## Editor frame and layout

- Use a 720 x 480 logical canvas with a 3:2 aspect ratio. Constrain resizing to
  540 x 360 through 1440 x 960 and provide a visible bottom-right resize handle.
- Scale the logical canvas uniformly. Do not scale font sizes independently with
  viewport width or rearrange controls merely because the host changed size.
- Use three stable regions: a compact command bar, the primary sound workspace,
  and a restrained status or metering area. Advanced settings open as an
  anchored panel, not a new page.
- Keep the next visual region partially visible only when a screen is scrollable.
  The plugin editor itself should fit without scrolling at its minimum size.
- Use spacing, alignment, and subtle separators for grouping. Do not nest cards
  or turn every group into a floating panel.

## Visual system

- Define colors, spacing, corner radii, strokes, and type styles as shared tokens
  in `VektLookAndFeel`; product editors consume tokens instead of literal values.
- Use a restrained neutral foundation with distinct semantic accent, warning,
  error, and meter colors. Do not encode state through color alone.
- Keep control and panel radii at 8 logical pixels or less.
- Use one expressive display face sparingly and a highly legible UI face for
  labels and values. Letter spacing is zero; compact panels use compact type.
- Render icons with JUCE paths or the selected icon library. Do not use text in
  a rounded rectangle when a standard icon communicates the command clearly.

## Parameter controls

- Use rotary controls for continuously adjusted primary parameters, horizontal
  sliders for ranges where position comparison matters, toggles for booleans,
  and segmented controls for two or three mutually exclusive modes.
- Every parameter control shows a stable name and formatted value with units.
  Value text must remain readable at minimum editor size and must not resize its
  surrounding layout.
- Dragging uses vertical motion by default. Shift-drag provides fine adjustment.
  Double-click resets to the declared parameter default.
- A focused control supports arrow-key adjustment; Shift plus arrow uses the fine
  step. Return opens numeric text entry when the value is editable.
- Numeric entry accepts the displayed unit, clamps through the parameter range,
  and reports invalid input without changing the existing value.
- Begin and end JUCE parameter gestures around pointer or keyboard edits. APVTS
  attachments remain the source of truth for automatable controls.
- Context menus provide reset and direct value entry. Product-specific actions
  may be added only when they are meaningful for that parameter.

## Control states

- Define normal, hover, pressed, keyboard-focus, disabled, pending, and error
  appearances for every interactive component.
- Keyboard focus is always visible and is stronger than hover. Disabled controls
  remain legible but clearly inactive.
- Pending state means the requested value differs from the active processor
  value. Show both values when ambiguity would affect sound or latency.
- Do not use animation, color, or a transient message as the only indication of
  an error or pending operation.

## Commands and navigation

- Use icon buttons for undo, redo, previous, next, save, import, export, bypass,
  settings, and close actions. Every icon button has a tooltip and accessible
  name; unfamiliar commands may include a short text label.
- Disable unavailable commands instead of accepting them and failing later.
- Use standard macOS shortcuts where applicable: Command-Z for undo,
  Command-Shift-Z for redo, and Escape to dismiss transient surfaces.
- Tab order follows visual reading order: command bar, primary controls,
  secondary controls, meters, then advanced settings.
- Opening a menu or dialog moves keyboard focus into it. Closing restores focus
  to the command that opened it.

## Bypass and quality

- Host bypass is a primary binary control and visibly distinct from wet/dry mix.
  Its state must mirror the host-exposed bypass parameter.
- Quality factor and phase live in an advanced panel because they affect latency
  and are not automatable sound controls.
- Label phase modes literally as `Minimum Phase` and `Linear Phase`; do not rank
  either as universally better.
- Disable phase selection when oversampling is Off.
- During playback, show a deferred quality request as pending while retaining a
  clear indication of the active mode. Remove pending state only after the
  processor applies the change and updates host latency.

## Metering and status

- Meters communicate current signal state and never become the main visual
  hierarchy. Initial metrics are stereo input peak, output peak, and gain delta.
- Audio threads publish lock-free scalar values. UI timers perform ballistics,
  decay, clipping hold, repaint throttling, and all text formatting.
- Use a stable decibel scale, label meaningful reference points, and reserve a
  distinct non-flashing state for clipping. Meter motion must not resize layout.
- Stop or substantially reduce repaint activity when the editor is hidden.

## Feedback, errors, and destructive actions

- Apply reversible parameter actions immediately. Confirm irreversible actions
  such as deleting a user preset and name the affected item.
- Keep validation errors adjacent to the field or control that needs correction.
  Preserve entered values and focus so the user can repair the problem.
- Use concise actionable language: state what failed and what can be changed.
  Do not expose implementation terms, paths, or error codes unless requested.
- Avoid success notifications for ordinary immediate actions. Use transient
  status only for asynchronous or otherwise non-obvious completion.
- Never block audio processing with file access, modal waits, logging, or UI
  synchronization. File operations are initiated and completed on the message
  thread.

## Accessibility

- Every control has an accessible role, name, current value, units, and state.
- Maintain at least 4.5:1 contrast for normal text and 3:1 for large text,
  controls, focus indicators, and meaningful graphics.
- Interactive targets are at least 28 x 28 logical pixels, with 36 x 36 preferred
  for primary controls at the minimum editor size.
- Do not rely on hue, position, animation, or audio feedback alone.
- Respect reduced-motion preferences and verify keyboard-only operation and
  VoiceOver reading order before release.

## Motion and transient surfaces

- Motion explains state changes; it is not ambient decoration. Prefer short
  fades or position transitions under 180 ms and avoid spring or looping motion.
- Menus, tooltips, dialogs, and advanced panels remain within editor bounds at
  every supported size and Retina scale.
- Tooltips identify controls; they do not carry essential instructions. Keep
  persistent help text out of the primary workspace.

## Component and validation contract

- Reusable UI components depend on state interfaces, not product processors or
  DSP implementations. Product editors compose components and provide labels,
  ranges, formatting, and commands.
- Components use deterministic logical bounds and expose state without reading
  mutable processor containers during painting.
- Test minimum, default, and maximum sizes at 1x and 2x scale. Verify no clipped
  text, overlap, layout shift, inaccessible controls, or out-of-bounds popups.
- Exercise mouse, keyboard, VoiceOver, automation, undo/redo, host bypass,
  deferred quality changes, preset modification state, and editor reopen/restore
  in Standalone, VST3, and AUv3 hosts.
