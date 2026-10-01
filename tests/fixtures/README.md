# Compatibility fixtures

Safety nets for changes that must not alter what users have already saved or heard, such as
framework, format-wrapper or state-code changes. Run them with `ctest --test-dir build/dev -L compat`
(about 2 s). Nothing here is read with JUCE: blobs are raw host state, expectations are plain
text, audio is 32-bit float WAV.

**Before the first release**, nothing has been saved by users, so a failure means "this changed",
not "users are broken". A deliberate change is allowed: replace the affected fixture and say why
in the commit. Accidental changes are what these catch. **From the first release on**, state and
parameter fixtures are binding: old ones are never edited or removed, only added to.

## `state/<product>/<date>.state`

The exact bytes `getStateInformation` returned on that date, with every parameter away from its
default, a factory program selected and product extras (Rav's stage order) changed.
`<date>.expected` lists what restoring it must produce.

- After release, never edit or regenerate a fixture; a format change must keep restoring all of
  them. Before release, a deliberate format change may delete the fixtures and freeze new ones.
- When a parameter is added, the coverage test fails. Freeze the current format as a new
  fixture with `build/dev/tests/vekt_dsp_tests "[.capture-state]"`. It refuses to freeze a state
  that does not round-trip, and never overwrites.
- Older fixtures also check that parameters added since then restore to their defaults, even
  over a modified instance.

## `parameters/<product>.txt`

One line per parameter: ID, version hint, kind, range, interval, skew, default, choices and
whether it is automatable. Hosts find automation and saved values by these, so a changed or
removed line fails the test. Order is not pinned, because JUCE addresses parameters by ID.

- New parameters fail the coverage test until they are added with
  `build/dev/tests/vekt_dsp_tests "[.capture-parameters]"`. Capture only appends and refuses
  while a frozen line has changed.
- Before release, change or remove a parameter by editing its line by hand, so the change shows
  up in review. After release, lines are permanent.

## `audio/<product>/<case>.wav`

0.2 s reference renders of the cases in `tests/compat/ReferenceRenderTests.cpp`. A fixed sweep
plus noise for the effects, and a fixed two-note phrase for Mono, at 48 kHz in 256-sample blocks.
The tolerance is a peak error of 2e-5 (about -94 dBFS). A failing case writes its render to
`build/dev/tests/compat-actual/` so it can be compared by ear.

- A deliberate sound change is accepted by recapturing with
  `build/dev/tests/vekt_dsp_tests "[.capture-references]"` and reviewing the WAV diff. Capture
  rejects cases that are not deterministic, are silent, or render identically to another case.
