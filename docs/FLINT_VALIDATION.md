# Flint Validation

Flint is a sample-free percussion synthesizer: one instrument per plugin instance, built into kits by the host (an
Ableton Drum Rack holds one Flint per pad). Its hierarchy is **Mode → Model → Sound**: Mode is the kind of instrument
(Kick, Snare, Mallet …), Model the synthesis method that makes it. This document is Flint's product contract and the
persisted plan for Milestone 1. Architectural decisions that freeze at release are recorded in
[ADR 0011](decisions/0011-flint-engine-architecture.md); naming follows [PRODUCT_NAMING.md](PRODUCT_NAMING.md).

Flint is pre-release (3 October 2026): no project, preset or parameter format is binding until the first release.

## Status

| Step | Scope | Status |
| --- | --- | --- |
| 0 | This contract, the mode check, the Kick / Classic Analog and Mallet / Bar model sheets, ADR 0011 | Approved by Thomas, 3 Oct 2026 |
| 1 | Move `LinearTptSvf` into `vekt_dsp` (Flint's Tone and Click filters); Mono bit-identical | Done 3 Oct 2026 (evidence below) |
| 2 | Scaffold `plugins/flint`; register Flint in `scripts/test-affected.sh` and `TestTagPolicy.cmake` | Done 3 Oct 2026, with step 3 (evidence below); the plugin target and factory data move to steps 7 and 10, which first need them |
| 3 | `FlintEngineHost`, output stage with Drive Type, stub engine, allocation counter | Done 3 Oct 2026 (evidence below) |
| 4 | `ModalBank`, then Kick / Classic Analog | Done 3 Oct 2026 (evidence below) |
| 5 | Mallet / Bar | Done 3 Oct 2026 (evidence below) |
| 6 | Hit hash and Variation; engine milestone (T2, review) | Done 3 Oct 2026; review findings fixed (evidence below) |
| 7 | `flint::PluginProcessor`, quality, `PresetHost`, seed, wrappers, `pluginval-dev.sh` | Done 3 Oct 2026 (evidence below); a generic editor stands in until step 9 |
| 8 | Compat registration and fixture capture | Done 3 Oct 2026 (evidence below) |
| 9 | Editor | Done 3 Oct 2026 (evidence below); visual review (A21) open |
| 10 | Factory presets | Done 3 Oct 2026 (evidence below) |
| 11 | Release cost tool (A19) | Tool done 3 Oct 2026; A19's idle ratio FAILS as written (evidence below), awaiting Thomas's decision |
| 12 | Script, packaging and skill registrations | Done 3 Oct 2026 (evidence below) |
| 13 | Architecture, UI and workflow documentation | Done 3 Oct 2026 |
| 14 | Final T3, listening, host checks, review | Automated part done 3 Oct 2026 (evidence below); A19, A21, A22, A23 open |

**Next action:** Thomas: decide A19's idle criterion, A4 under linear-phase FIR and the Velocity/Variation placement
(step 14 evidence); review the editor (A21) and listen to `build/flint-audition` (A22); approve an install for auval
and the Ableton smoke test (A23).

**Blockers:** none. The maker rename landed in `7277e5c`. Reference recordings for listening (A22) are Thomas's to
supply.

**Evidence, step 1 (3 October 2026, uncommitted on `7277e5c`):** `LinearTptSvf` moved to `vekt/dsp/LinearTptSvf.h`
unchanged but for its namespace. Mono's audio code never used it (only Mono's SVF, nonlinear SVF and K35 tests, as a
reference), so Mono's sound cannot change. T0: the 43 cases of those three test files in `dev-opt`, 41 passed; the
other 2 are hidden audition renders that need `VEKT_MONO_DUMP` (not applicable). T1 `scripts/test-affected.sh`: 490/490
in `dev-opt`, 12/12 compat in Debug including "Every Mono reference render still sounds the same" (A20). T2
`scripts/test.sh --t2`: 456/456 in Debug, slow 45/45 in `dev-opt`; `scripts/lint-changed.sh` passed. T2's budget check
failed on two runs on Mono tests near the 3 s limit under parallel load (3.4 s and 3.7 s, then 3.0 s; 2.1 s and 2.2 s
alone in Debug): "Mono linear SVF stays exact at the lowest cutoff and highest effective rate" (its code is unchanged)
and "Mono high-pass ladder is the low-pass ladder's mirror at small signals" (untouched). Reported, not fixed.

**Evidence, steps 2 and 3 (3 October 2026, uncommitted on `7277e5c`):** `FlintCore` (`plugins/flint`) with the model
table (`Models.h`), the parameter snapshot, the `FlintEngine` interface, `FlintEngineHost`, `DriveStage` and
`SafetyClip`; Flint registered in the root CMake file, `vekt_dsp_tests`, `TestTagPolicy.cmake` and
`scripts/test-affected.sh`. The allocation counter (`tests/flint/AllocationCounter.h`) uses `malloc_logger`: patching
the default zone and dyld interposing were probed first; the zone patch counted nothing, interposing double-counted.
T0: 15 `[flint]` cases pass in `dev-opt` and Debug (A1 and A15 at host level, A2, A3, A12, A26's finiteness, the
safety clip). Each was shown to discriminate: 11 deliberate defects (no fade, no fade reversal, a third engine cutting
in, a faded-in new engine, a pending preset ignored, never sleeping, allocating while rendering, Drive never bypassed,
Drive not normalized, Fold dividing by zero, Drive switching instantly) each failed its test. T1: full suite (CMake
changed) 513/513 in `dev-opt`, 12/12 compat in Debug. T2: 471/471 Debug, 45/45 slow, budgets PASS;
`scripts/lint-changed.sh` PASS after formatting the new files with the pinned clang-format 21.1.8.

**Evidence, steps 4–6 (3 October 2026, uncommitted on `7277e5c`):** `ModalBank` (with the per-mode cap), Kick /
Classic Analog, Mallet / Bar, the hit hash and the Variation rules; 51 `[flint]` cases. Research: the Werner, Abel and
Smith paper gives the bass drum's structure and behaviour but no component values (they are in Roland's service
notes), so the Kick follows its structure with named voicing constants. Design changes found by the tests, recorded in
the sheets above: the Bar's contact became `t e^(−t/θ)` (the Gaussian left the default mallet dull); its contact noise
rose 4 dB, grows as `v^1.5` and keeps its energy whatever the contact's duration; the Kick's beater noise gained its
own 2 ms decay at +3.5 dB (the click's hit-to-hit correlation fell from 0.98 to at most 0.66); Attack and Hardness are
level-normalized by predicted peak; A7's Bar criterion was amended (see A7). Tests fixed after review of their own
failures: a circular pitch reference, a length mismatch, windows that never reached their measurement levels, and a
measurement helper costly enough to push Mono tests over budget under parallel load. Mutation checks: 35 deliberate
defects across the engine host, Drive, `ModalBank`, Kick, Bar and Variation, each caught by its test after the fixes
(one, removing the Bar's whole-object energy limit, showed that limit redundant; it was removed). T0: 51/51 `[flint]`
in `dev-opt`. T1: full suite 549/549 in `dev-opt`, 12/12 compat in Debug. T2: 505/505 Debug, 47/47 slow, budgets PASS;
`scripts/lint-changed.sh` PASS. Not yet run: listening (A22).

**Evidence, engine review and steps 7–8 (3 October 2026, uncommitted on `7277e5c`):** the `vekt-reviewer` pass on the
engine milestone found two high and four medium defects, all fixed and recorded in the sheets above: Drive's own
output (crossfade, ramp, anti-aliasing memory) did not keep the object awake, so its tail was cut (H1); Level and Drive
ramps left stale while asleep glided into the next hit (H2); the Bar cut a mode crossing 20 kHz instead of fading it
(M2); the Kick's strike prediction ignored the sweep, so swept hits were up to 8.6 dB louder than nominal (M3); hit
noise was not band-limited, so its level changed with the internal rate (M4); and a Kick strike's prediction ran at
the internal rate, so its cost grew with quality (M1: now 96 kHz at most; its cost is measured in step 11). Lows fixed:
`ModalBank` sleeps by output, not state, energy; the Bar's pulse sum is finite and closed-form; the tube's level moves
per sample and the tube is cleared at 0. Each fix has a test shown to fail without it (mutation checks; the ceiling fade is measured as
the click in the empty 12–14 kHz band, since a 20 kHz mode steps by nearly its whole amplitude every sample anyway).
Step 7: `flint::PluginProcessor` (output-only stereo, MIDI split at sample offsets, quality with deferred activation and
reported latency, `PresetHost` with factory programs, the seed in project metadata, Mode start values in one undo
step), the VST3, AU and Standalone wrappers, and `scripts/pluginval-dev.sh flint`; 13 processor cases (A3, A4, A2 at
processor level, A8, A9, A11, A13, A31's undo). Step 8: Flint registered in `tests/compat/CompatFixtures.h`; parameter
fixture `tests/fixtures/parameters/flint.txt` and state fixture `tests/fixtures/state/flint/2026-10-03` captured
(approved in the plan). T0: 73/73 `[flint]` in `dev-opt`. T1: full suite 571/571 in `dev-opt`, 12/12 compat in Debug.
T2: 526/526 Debug, 48/48 slow, budgets PASS; `scripts/lint-changed.sh` PASS (after fixing one widening cast);
`scripts/pluginval-dev.sh flint` SUCCESS at strictness 10, no GUI tests.

**Evidence, step 9 (3 October 2026, uncommitted on `7277e5c`):** `flint::PluginEditor` replaces the generic editor
(layout in `docs/UI_UX.md`). Shared framework additions, each inert for the other products: a disabled `ModeButton`
is captioned UNAVAILABLE; `RotaryControl::setDragSnap`; `VektLookAndFeel` dims a rotary ring outside its
`playableFrom`/`playableTo` span. `Models.h` gained `isAvailable` and `pitchRangeOf` (the Kick and Bar now clamp
Pitch from that table), `Parameters.h` the model-control and model-parameter tables. Found by the editor tests: the
selection callbacks ran before the state's raw values caught up (JUCE notifies the newest listener first), so the
editor reads the parameters' own values; unit lines trailed changes until the next timer tick, so they now follow
every slider. Checks: "Flint editor keeps the shared controls in place and rebinds the model's own" (A21's
automatable parts at 1120×700 and 2240×1400: in-bounds and no overlaps, nine modes with the unavailable ones
disabled, shared controls fixed across a Mode change, slots rebinding, Pitch readout, clamped note and dimmed span,
semitone snapping and text entry, Drive Type under Drive, Settings contents, New Seed, the active quality, the editor
following undo and redo of a Mode change); "Rotary drag snapping rounds dragged values only"; "Flint builds an
engine for exactly the models it marks available". `Flint_Standalone` and `Flint_VST3` build in Debug. T0: 75/75
`[flint]` in `dev-opt`. T1: full suite 574/574 in `dev-opt`, 12/12 compat. T2: 529/529 Debug, 48/48 slow, budgets
PASS; `scripts/lint-changed.sh` PASS; `scripts/pluginval-dev.sh --gui flint` SUCCESS at strictness 10 with GUI tests.
Snapshots (`VEKT_FLINT_SNAPSHOT`, `_MALLET`, `_SETTINGS`) were reviewed by Claude only; A21 needs Thomas's review.

**Evidence, step 10 (3 October 2026, uncommitted on `7277e5c`):** `tests/presets/FlintPresetTests.cpp`: every factory
preset is complete and meets its sheet target in the sheets' units (for example Tight's T60 0.12 s, Sweep 3 semitones
over 15 ms; Vibraphone's T60 6 s at F3), loads unmodified and leaves quality and the seed alone; a preset from another
product, a missing, unknown, out-of-range or non-finite value and a Mode beyond the list are each rejected with the
sound unchanged; Note Off Damps is exact after fractional automation; a modified user preset and the seed come back
with the project (A17). Detuning Vibraphone's Decay made the target test fail. 7 `[flint][presets]` cases pass.

**Evidence, step 11 (3 October 2026, uncommitted on `7277e5c`):** `VektFlintCost` (`tools/audio_lab/FlintCostMain.cpp`,
`audio-lab-release`), run alone on Thomas's Mac, 48 kHz, 128-sample blocks, 30 s runs after 10 s screening, no
allocation in any callback:

| Run | Mean per callback | p99.9 | CPU |
| --- | --- | --- | --- |
| Empty JUCE instrument (before, after) | 0.019 µs, 0.017 µs | 0.042 µs | 0.0007 % |
| Flint idle | 1.081 µs | 1.208 µs | 0.041 % |
| Kick / Classic Analog, struck at 8 Hz | 9.58 µs | 83.9 µs | 0.36 % |
| Mallet / Bar, struck at 8 Hz | 5.66 µs | 35.0 µs | 0.21 % |
| Kick at 16x, 192 kHz, 64-sample blocks | 89.2 µs | 236.6 µs (max 276.5 of 333.3) | 26.8 % |
| Bar at 16x, 192 kHz, 64-sample blocks | 65.3 µs | 116.8 µs | 19.6 % |

A19 as written FAILS: idle Flint is 59x the empty instrument, not at most 2x. The empty instrument only clears its
buffer (0.02 µs); Flint's idle callback also reads its parameters, updates the engine host and publishes the meter and
oscilloscope that every Vekt product publishes, which alone exceeds 2x. Flint's idle work is 0.04 % of the deadline and
it does no engine work while silent (A2). The criterion needs Thomas's decision (for example an absolute bound such as
"idle below 0.1 % of the callback deadline at 48 kHz", or a baseline that publishes the same meter and scope); it is
not changed here. The 16x Kick strike is M1's worst case: within the deadline, with 17 % headroom at its slowest.

**Evidence, steps 12–13 (3 October 2026, uncommitted on `7277e5c`):** Flint registered in `scripts/build-dev.sh`,
`build-au.sh`, `install-vst3.sh` (edited, not run), `validate-release.sh`, `verify-bundles.sh`, both watch scripts,
`packaging/macos/validate-plugins.sh` and its README (`aumu Flnt Tava`), `tests/cmake/PluginWorkflowTests.cmake` and
the `vekt-validate` skill; documented in `ARCHITECTURE.md` (dependency graph, instrument and effect DSP contracts,
quality defaults, preset path), `UI_UX.md`, `DEVELOPMENT_WORKFLOW.md` and `CLAUDE.md`. "macOS plugin workflow scripts"
passes (6/6 `scripts`-labelled cases); `scripts/build-dev.sh` builds all four products and every Debug Standalone,
VST3 and AU bundle passes strict signature verification; `scripts/build-au.sh` builds. Nothing was installed.

**Evidence, step 14 (3 October 2026, uncommitted on `7277e5c`):** hidden listening renders, "Flint audition renders"
(`[.][flint-audition]`, `VEKT_FLINT_AUDITION=<folder>`): 23 WAVs of A22's cases, peaks −4 to −8 dBFS, all finite,
copied to `build/flint-audition`. The final `vekt-reviewer` pass found no high-severity defect. Fixed, each with a test
shown to fail without the fix: a released note's Note Off damping carried into the next hit on the silent object
(M1, breaking A16 a; `activate` now ends it); the Drive half of the idle-change fix had no test (M2: Drive changes while
asleep now covered); model-slot readouts lagged a timer tick (L1). Also fixed: Drive ramps alone no longer keep a silent
object awake (L3, tested; with silent input Drive outputs zeros unless anti-aliasing memory remains), a Mode change
flushes pending edits before its undo step (L2), the preset hold is read before the snapshot (L5), Pitch's range has one
source (`fullPitchRange`), the cost tool plays the Bar at its start values (L9), the editor test checks Velocity and
Variation and that a rebound slot drives its own parameter (L10), and the Kick's level at 96 kHz and the highest
internal rate is asserted (within 0.5 dB). The Drive wake guard in the host (`drive.isRinging()`) is now redundant in
effect: `finishHost` already stays awake while the internal block holds any non-zero sample, so removing it changes no
test. Open for Thomas: (a) A19's idle criterion (step 11); (b) A4 is tested at Off only, and its wording ("first
non-zero sample at n plus the latency") cannot hold under linear-phase FIR, which rings before the main tap; (c) the
contract places Velocity and Variation "beside the shared controls", the editor beside the model's controls under the
shared row (A21); (d) the 16x Kick strike's 17 % headroom on this Mac (two instances or a slower machine would
overrun). Final checks: T1 `scripts/test-affected.sh` 581/581 in `dev-opt` and 12/12 compat; T3 `scripts/test.sh`
584/584 passed but the budget check FAILED on two runs, each time on different Mono cases (4–7 of them, 3.1–68.9 s
against their limits; no Flint case above 2.7 s) while another build kept the load average near 62;
`scripts/lint-changed.sh` PASS; `scripts/pluginval-dev.sh --gui flint` SUCCESS at strictness 10. The step 11 Bar
figures were taken at the Kick's Pitch (A1, clamped to C2); the rerun at the Bar's start values (C4) gave a 0.23–0.47 %
mean but a 9.7 ms maximum under that same load, so it is not evidence: A19's runs must be repeated on an idle machine.

## Identity

Flint takes every maker field from the shared constants in the root `CMakeLists.txt`, never its own literals:
`COMPANY_NAME "${VEKT_MAKER_NAME}"` (Thomas Vaags), `PLUGIN_MANUFACTURER_CODE ${VEKT_MAKER_CODE}` (`Tava`) and
`BUNDLE_ID ${VEKT_BUNDLE_ID_PREFIX}.flint` (`com.thomasvaags.flint`). Its own fields: `PRODUCT_NAME "Flint"`,
`PLUGIN_CODE Flnt`, AU type `aumu` (`auval -v aumu Flnt Tava`), VST3 categories "Instrument Drum". User presets resolve
through `PresetPaths::desktop("Flint")` to `~/Library/Audio/Presets/Thomas Vaags/Flint`. The preset document's product
identifier follows the other products' form when the rename settles it (`com.vekt.<product>` today).

## Product Principles

- **No voice allocator.** Each instance owns one persistent sound-making system; a new note strikes it again and never
  cuts its ringing tail. Retriggering either **restarts** (envelopes start over, analog models) or **re-excites** (adds
  energy to the ringing object, modal models).
- **Only the selected model uses CPU.** Unselected engines are never processed; a silent instance outputs exact zeros.
- **Six shared controls** keep their place for every Mode and Model: **Pitch, Attack, Decay, Tone, Drive, Level**.
  Each model implements them its own way. Below them sit four or five controls of the selected model.
- **Velocity is excitation strength**, not just volume.
- **Lightweight is a goal, not a limit.** A model may cost more for better sound; idle silence, unselected engines
  doing no work and Drive at zero being skipped stay hard requirements.
- Out of scope: samples, a sequencer, an internal kit or mixer, multiple outputs, a modulation matrix, LFOs, modular
  routing.

## Modes and Models

The V1 list; Milestone 1 builds the two marked models. Names are neutral: no drum-machine names in the UI.

| Mode | Models |
| --- | --- |
| Kick | **Classic Analog**, Punch Analog, Membrane, FM |
| Snare | Classic Analog, Punch Analog, Membrane + Wires, FM + Noise |
| Tom | Classic Analog, Punch Analog, Membrane, Hand Drum, FM |
| Clap | Analog Burst, Hand Ensemble |
| Hi-Hat | Classic Metal, FM Metal, Acoustic |
| Cymbal | Classic Metal, Acoustic, FM Metal |
| Shaker | Particle, Classic Analog |
| Mallet | **Bar**, Plate, Bell, FM |
| Percussion | Classic Analog, Wood, Metal, FM |

| Family | Approach | Models |
| --- | --- | --- |
| Analog Resonator | 808 lineage: trigger/accent pulse into a resonant core, output tone filter (Werner, Abel and Smith, DAFx 2014) | Kick, Snare, Tom Classic Analog |
| Analog Oscillator | 909 lineage: pitch-swept oscillator, waveshaper, click and noise | Punch Analog models |
| Metal Network | Square oscillators with a Sum↔Ring/XOR control and band filters; the cymbal's three decaying bands (Werner, Abel and Smith, ICMC 2014) | Classic Metal models |
| Noise/Burst | Noise bursts with a small diffuser tail | Clap Analog Burst, Shaker Classic Analog |
| Modal | Contact mallet, tension modulation, approximate nonlinear mode coupling, serial body resonator, stereo mode amplitudes | Membrane, Membrane + Wires, Hand Drum, Acoustic, Bar, Plate, Bell |
| FM | Percussion-oriented FM | FM models |
| Particle | PhISEM (Cook, Computer Music Journal 1997); also schedules Hand Ensemble's events (Peltola et al., IEEE TASLP 2007) | Shaker Particle, Clap Hand Ensemble |
| Percussion exciter–resonator | Impulse, noise, contact or scrape exciter into 2–4 tuned resonators, with an oscillator-pair option | Percussion Classic Analog, Wood, Metal |

### Mode check (engine interface)

Needs of later modes the Milestone 1 interface must already carry, so it does not change when they arrive:

- **Clap:** one trigger schedules several delayed stereo sub-hits, possibly across block boundaries. Scheduling is
  internal to the engine and sample-accurate; `process` writes both channels.
- **Hi-Hat:** Two-Note Mode needs the note on each strike (closed/open) and a closed strike damps the open ring inside
  the engine. `Strike` carries the note.
- **Snare (Membrane + Wires):** two linked systems; `energy()` and `isActive()` cover both, so a ringing wire buzz keeps
  the engine awake.
- **Cymbal (Acoustic):** many modes; per-mode sleep must stay cheap at large counts (`ModalBank` keeps its state as
  structure of arrays and skips sleeping modes).
- **Ensemble:** memory for each model's maximum Count is reserved in `prepare`; Count changes never allocate.
- **Percussion exciter–resonator:** exciters are engine-internal; nothing beyond `Strike` crosses the interface.
- **Shaker / Particle:** one shot; Note Off is ignored unless Note Off Damps is on.

## Parameters

Parameter IDs, ranges and Mode/Model lists live in `include/vekt/flint/Parameters.h` only. Quality lists come from
`vekt/dsp/OversamplingChoices.h`.

| ID | Range | Default | Automatable |
| --- | --- | --- | --- |
| `flint.pitch` | MIDI note 12–108 (C0–C8), continuous (0.01-semitone steps); host text is note and cents ("D#2 +12 ct") | 33 (A1, 55 Hz) | yes |
| `flint.attack`, `flint.decay`, `flint.tone`, `flint.drive` | 0–100 % | 30 %, 40 %, 50 %, 0 % | yes |
| `flint.level` | −48 to +12 dB | 0 dB | yes |
| `flint.velocity` | 0–100 % sensitivity | 100 % | yes |
| `flint.variation` | 0–100 %: 0 identical hits, about 30 % natural, 100 % loose | 30 % | yes |
| `flint.noteOffDamps` | bool | off | yes |
| `flint.driveType` | Soft, Hard, Fold (grows at its end) | Soft | no |
| Tracking / Offline quality | `QualitySelection::makeTrackingParameter` / `makeOfflineParameter` | Off / Off | no |
| `flint.mode` | the nine modes (grows at its end) | Kick | no |
| `flint.kick.model` | Classic Analog, Punch Analog, Membrane, FM | Classic Analog | no |
| `flint.mallet.model` | Bar, Plate, Bell, FM | Bar | no |
| `flint.kick.analog.*` | sweep, sweepTime, click, bodyShape (Kick sheet) | | yes |
| `flint.mallet.bar.*` | material, hardness, position, overtones, resonator (Bar sheet) | | yes |

- Velocity: `v = 1 − s + s · velocity / 127` with sensitivity `s`; each model applies its own curve to `v`.
- Unimplemented modes and models play silence and are marked unavailable in the editor.
- The host shows Attack, Decay, Tone and Drive as percent; the editor shows the model's unit beneath (for example
  "1.2 s"). Pitch shows note and cents in the host and note, cents and frequency in the editor ("D#2 · 77.8 Hz").
  Dragging Pitch in the editor snaps to semitones; Shift-drag is continuous. Host automation is continuous, so
  automated slides are smooth.
- Pitch outside a model's useful range is clamped by the model; the editor shows the note actually played and dims the
  arc outside the range.
- **Mode start values.** Changing Mode in the editor also sets the shared controls to that mode's start values (the
  mode's classic sound), as one undo step; changing Model within a mode keeps them, so models can be compared on the
  same settings. Model controls keep their own values (they belong to their model). The table of start values lives in
  `Parameters.h`; Milestone 1: Kick Pitch A1, Attack 30 %, Decay 40 %, Tone 50 %; Mallet Pitch C4, Attack 30 %,
  Decay 45 %, Tone 50 %. Parameter defaults are the Kick's (the default mode). Presets and restores set every
  parameter, so start values apply only to a Mode change in the editor.
- **Editor placement.** Drive Type sits under Drive. Velocity and Variation form a visible pair beside the shared
  controls. The Settings panel holds Tracking/Offline quality, Note Off Damps and New Seed.
- Flint tracks and renders Off by default, so a bounce matches playback (Mono renders offline at 4x FIR).

## Signal Path

- **`flint::PluginProcessor`** composes and owns no DSP: the parameter layout (read through
  `plugin_support::requireParameter`), MIDI splitting at event positions, the host playhead, the hit hash and the
  stopped-transport counter, `QualitySelection` with `OversamplingBank`, `PresetHost` and the seed.
- **Parameter snapshot.** Once per block the processor fills one `FlintParameters` value with every Flint parameter;
  the engine host passes it to the selected engine (`update`), which reads its own slice and smooths within the block.
  Engines never see APVTS.
- **`FlintEngineHost`** owns engine selection, switching, idle sleep and the output stage:
  - Selection changes in one step on the audio thread, only from a complete parameter snapshot; while a preset is being
    applied no switch happens. After a preset load old audio is reset and a note in the same callback still sounds.
  - A switch fades a still-ringing old engine out over 5 ms; a silent one is reset at once. The new engine starts
    reset, silent and at full gain, so a strike right after a switch is never attenuated. Switching back during a fade
    reverses it without a reset; a third model selected during a fade waits for it to end, so no more than two engines
    ever run. A newly activated engine's smoothing starts at the current values.
  - Sleep needs an inactive engine, output-stage tails below threshold and the downsampling filter's latency elapsed;
    on sleep all state is flushed and the output is exactly `0.0f`.
  - Output stage inside the oversampled region: Engine → Tone (where the model places it after the engine) → Drive.
    At host rate: Level → DC blocker (`vekt::dsp::DcBlocker` at 5 Hz, wrapped so it is zeroed below threshold) →
    safety clip.
- **Engine interface** (Flint-only, audio paths `noexcept`): `prepare`, `reset`, `activate(const FlintParameters&)`
  when selected, `update(const FlintParameters&)` once per block, then per block segment `trigger(Strike)`,
  `release(note)` and `process(left, right)`; `isActive()` and `energy()`. `Strike` is a small value: velocity, note and the 64-bit hit hash; it
  takes effect at the start of the segment the processor renders next, so it needs no sample offset. An engine draws as
  many values as it needs from a counter-based generator (SplitMix64 of hash and draw index); each model documents its draw
  order.
- **Kick and Bar are mono:** both channels carry the same signal. Stereo arrives with the models that need it.

### Shared stages

- **`ModalBank`** (Flint-internal, two consumers: the Kick's single resonance and the Bar's modes). Each mode is a
  complex one-pole resonator `q ← p q + b u`, `p = r e^{iω}`, in double, stored as structure of arrays. The output is `Im(q)`. Retuning or re-damping changes `p` without touching `q`, so the output never steps;
  the energy `|q|²` is exact; a mode sleeps when, with no input, its output energy (`|q|²` times its output gain
  squared) falls below −120 dBFS, so a mode with a large output gain is not cut early. Input is differenced
  (`u[n] − u[n−1]`) where a model needs a band-pass response with no DC.
- **Strike build-up cap.** A strike's effect on mode `n` over its pulse, `Δ_n`, is computed in closed form from the
  pulse's spectrum at that mode, before it is rendered, and compared with the mode's state at the pulse's end, `q_n`.
  The strike is scaled for that mode by the largest `g ≤ 1` with `|q_n + g Δ_n| ≤ max(2 |Δ_n|, |q_n|)`. A mode can thus
  build up to twice one strike, but no further; modes that have decayed (high modes of wood, for example) take the full
  strike, so a hit on a loud tail still has its attack; strikes out of phase are never reduced. A strike on a silent
  object is unscaled (bit-identical). Since no mode exceeds twice a strike, an object's energy stays within +6 dB of one
  strike; its peak can rise a little more as its modes' relative phases change over many strikes (Bar: +6.2 dB peak,
  +6.1 dB loudest-10 ms RMS at Resonator 0, measured 3 October 2026). The Kick's prediction follows its time-varying
  pole (sigh, attack shift and sweep) at the engine's rate or 96 kHz, whichever is lower; the Bar's is exact for its
  sampled contact pulse, a finite closed-form sum.
- **Drive:** `y = f(G x) · 0.5 / m(G)` with input gain `G = 10^(36 d / 20)` for Drive `d` (0 → +36 dB) and `m(G)` the
  shaper's peak over the nominal input range `|u| ≤ 0.5 G`, so a −6 dBFS peak keeps its level (for Fold `m(G)` is
  `sin(π G / 4)` below `G = 2` and 1 above, never 0). Curves, each with first-order antiderivative anti-aliasing and the
  usual fallback to the midpoint value when consecutive inputs nearly coincide: Soft `tanh(x)`; Hard
  `clamp(x, −1, 1)`; Fold `sin(π x / 2)`. At Drive 0 the stage is skipped (bit-identical). The anti-aliasing delays the
  driven path by half a sample, so crossing zero while sounding crossfades the skipped and driven paths over 5 ms.
  While Drive still has output of its own (a crossfade, a ramp or anti-aliasing memory) the object stays awake; when it
  sleeps, Drive is reset.
- **Safety clip:** identity for `|x| ≤ 0.891` (−1 dBFS), a quadratic knee above it whose slope falls from 1 to 0, ceiling exactly 1.0 (0 dBFS).
- **Hit hash:** each hit's random values come from a hash of the instance seed, the hit's song position in beats
  rounded to 1/3840 beat, and its order among hits on the same sample. With the transport stopped or no playhead a
  per-instance counter replaces the position. The seed is drawn when the instance is created, saved in project
  metadata (not presets) and copied to an atomic for the audio thread; a preset load keeps it; New Seed redraws it.
- **Variation.** What defines the instrument stays fixed: the fundamental's pitch, the body's decay and level (an
  acoustic model may vary the strike force by at most ±1 dB, as a player does: the Bar), and the hit's start time (layered drums must stay phase-aligned). Variation acts on the transient and the upper spectrum:
  excitation texture, transient level and brightness, the balance of upper partials. Rules for every model:
  - Each varied quantity draws an approximately normal value (the mean of three uniform draws), clipped to the model's
    stated range at 100 %; ranges scale linearly with Variation.
  - Per-hit noise (beater, contact, wires, particles) has two realizations: a fixed one from a constant seed and a fresh
    one from the hit hash, mixed at equal power with the fresh share equal to Variation. At 0 every hit is identical; at
    100 % every hit's texture is new.
  - Noise is low-passed at 18 kHz and its gain normalized to the internal rate from the filter's measured noise
    bandwidth, so its level is the same at every quality and sample rate.
- **Pitch glide:** pitch changes ramp linearly in semitones over 15 ms (`vekt::dsp::LinearRamp`, double); the ringing
  object follows without a step. Host automation arrives once per block.
- **Changes while idle:** a sleeping object's ramps (Pitch, Level, Drive, model controls) jump to their targets at the
  next strike, so a change made between hits never glides into the next one.
- **Gain and damping changes** (Tone's mode tilt, Note Off damping, Decay automation) ramp over 2–15 ms; mode gains are
  interpolated per sample between values computed every 32 samples.
- **Precision:** per-sample state that steps toward a target or accumulates (glide, sweep envelope, resonator
  rotation, mode decay) is `double` and stays correct at `vekt::dsp::maximumInternalSampleRate` (3.072 MHz).

## Classic Models: Reaching the Original

Every Classic model (Classic Analog, Classic Metal, Punch Analog) can make its original machine's sound, a classic
808-style snare for example, and goes beyond what the machine could do. Its controls are designed for musical drum
design, not copied from the machine's knobs. There is no "faithful" mode, marker or band in the user interface.

- **The sound comes from the circuit.** Each model starts from its original's published circuit model (nominal
  component values), because that is where the character lives. Code may integrate extensions wherever they sound best
  (Sweep acts inside the Kick's resonance); no separate circuit-core class or bit-identity to the circuit is required.
- **Controls follow musical dimensions.** Each model control changes one perceptual attribute, monotonically, named for
  what is heard. An original knob whose effect a shared control covers folds into it (the 808 kick's Decay and Tone
  become the shared Decay and Tone). An original knob whose name clashes with a shared control is renamed for its effect
  (the 808 snare's "Tone", a balance of its two body resonances, would not be called Tone).
- **The original is easy to find.** The model's defaults land on the classic sound, and it sits mid-range, so every
  control can move both ways from it. A factory preset named for its sound ("Classic") returns to it.
- **Extension controls at 0 add nothing** (Click, Sweep, Body Shape): predictable, and the stage is skipped.
- **Free-running sources stay free.** Where the original's hits differ because a source runs freely (the snare's noise,
  the metal oscillators' phases), that source starts fresh on every hit whenever Variation is above 0. Variation 0
  freezes it, which the machine could not do.
- **Velocity is excitation strength** through the circuit's accent behaviour, continuous where the original had two
  levels.
- **Reaching it is verified by measurement and listening**, not by parameter positions: the Classic preset matches the
  reference (the paper's figures, reference recordings) on measured descriptors and in an A/B listen.

## Model Sheet: Kick / Classic Analog

**Character:** the 808-lineage bass drum: a pulse-struck resonance, close to a sine, from a short thud to a long sub
boom. Retrigger: **restart** the sweep while the resonance keeps ringing and is struck again.

**Sound source**, following the circuit model of Werner, Abel and Smith (DAFx 2014). The paper gives the circuit's
structure and behaviour but not its component values (they are in Roland's service notes, which it cites), so where it
leaves a number open the model uses a named voicing constant (`KickClassicAnalog.h`, step 4, 3 October 2026):

- **Pulse shaper:** the trigger logic's 1 ms pulse, its amplitude the accent voltage (4–14 V, velocity mapped
  linearly); the shaper's low shelf passes the rising edge and settles to a plateau (`shelfRatio` 0.1); the falling edge
  is diode-clamped at 0.71 V whatever the accent. Its fall time is Attack's.
- **Resonance:** the paper's bridged-T transfer function is `H(s) = 1 + b s / (a₂ s² + a₁ s + 1)`: the shaped pulse
  passes straight through (the circuit's own click, `directGain` 0.2 of the body) and drives a band-pass resonance,
  here one `ModalBank` mode with differenced input. For the first 6 ms the envelope generator holds it more than an
  octave higher (`attackShift` 2.3); afterwards leakage makes its pitch "sigh" down as its amplitude falls (frequency
  lift `0.14 · min(A², 4)` at relative amplitude `A`, after Fig. 11's 48–58 Hz). The complex resonator keeps its energy
  across the attack shift, so the circuit's retriggering pulse, which restores energy the analog filter loses, is not
  needed. Sweep adds to the resonance's frequency.
- **Tone stage:** the circuit's first-order passive low-pass, its cutoff range extended beyond the original knob.
- **Level:** a full-accent hit's resonance is set to `bodyLevel` (0.356), so a default hit peaks at −6 dBFS. Attack
  keeps the level: each strike is scaled so the resonance peaks as after the circuit's own pulse.
- **Order:** resonance → Body Shape → + Click → tone stage, so Tone darkens the click with the body.

**Model controls** (each skipped at 0):

| Control | Range and curve | Default |
| --- | --- | --- |
| Sweep | extra onset pitch `36 s²` semitones (0–3 octaves), added to the resonance's frequency | 0 % |
| Sweep Time | sweep decay time constant `2 ms · 250^t` (2–500 ms) | 30 % (10.5 ms); irrelevant at Sweep 0 |
| Click | the beater: the pulse plus beater noise (+3.5 dB re the pulse's rising edge, decaying from the strike with a 2 ms time constant, so it outlasts the 1 ms electrical pulse and gives each hit its own texture), through a `LinearTptSvf` 2-pole high-pass at 1.5 kHz, added before the tone stage; 100 % peaks at the body's nominal peak | 0 % |
| Body Shape | `P · tanh(g x / P) / tanh(g)` with `g = 5 b²` and `P` the nominal body peak: the identity at 0 (stage skipped), a rounded square at 100 %, level kept at the nominal peak | 0 % |

| Shared control | Meaning in this model |
| --- | --- |
| Pitch | resonance fundamental; useful range B0–C5 (MIDI 23–72, 30.9–523 Hz) |
| Attack | pulse fall time: 0 % 0.1 ms, 30 % the circuit's own pulse, 100 % 2 ms, exponential in each segment |
| Decay | resonance T60 `40 ms · 200^d` (40 ms–8 s) |
| Tone | the circuit's tone stage, cutoff swept exponentially beyond the original knob's range at both ends |
| Drive | shared output-stage Drive |

The shared defaults (Pitch A1, Attack 30 %, Decay 40 %, Tone 50 %) give the classic sound; step 4 adjusts the Decay
and Tone curves from the paper so their defaults land on the original's typical setting, mid-range.

- **Retrigger:** the sweep restarts at its top immediately; `ModalBank` keeps the output continuous while the frequency
  jumps, so the ringing tail chirps up with the new strike.
- **Velocity:** strike strength through the circuit's accent behaviour; sweep depth `× (0.6 + 0.4 v)`; click level
  `× v²`.
- **Variation (at 100 %), in draw order:** click level ±3 dB, click high-pass cutoff ±4 semitones, sweep depth ±10 %,
  sweep time ±10 %, then the beater-noise seed. The body never varies; the original kick has no free-running source, so
  with Click and Sweep at 0 its hits repeat exactly, as on the machine.
- **Note Off Damps:** Note Off moves the resonance's T60 to 60 ms over 2 ms (an extension; off by default).
- **Calibration:** with defaults, a full-velocity hit peaks at −6 dBFS ±1 dB after the tone stage, before Drive and
  Level. Sweep never raises it (at most +0.5 dB); a fast, deep sweep starting above the tone stage's cutoff loses up to
  3.2 dB there (measured 3 October 2026).
- **Idle:** inactive when the resonance, sweep, pulse and click are all below −120 dBFS; the state is then zeroed.

**Factory preset targets:** Classic (the circuit at the original's stock knob positions, model controls at 0), Long
Boom (Decay 3 s, Body Shape 30 %, Drive Soft 20 %), Tight (Decay 120 ms, Sweep 3 semitones over 15 ms, Click 30 %),
Distorted Sub (Decay 1.5 s, Drive Fold 35 %).

## Model Sheet: Mallet / Bar

**Character:** struck tuned bars from rosewood marimba and xylophone to metal vibraphone and glockenspiel, with an
optional tube resonator. Retrigger: **re-excite** the ringing bar.

**Topology.** A `ModalBank` of up to 12 modes driven by a contact-force pulse at the strike position, optionally
followed in series by a tube resonance at the fundamental.

- **Mode frequencies.** A uniform free-free bar has ratios 1, 2.756, 5.404, 8.933, 13.34, 18.64, 24.81, 31.87 …
  Overtones moves modes 2 and 3 in log-frequency, piecewise linearly: 0 % uniform (2.756, 5.404), 50 % xylophone
  (3, 6), 100 % marimba (4, 10). Modes 4 and up keep the uniform spacing scaled by mode 3's factor. Modes above 20 kHz
  are dropped, at every sample rate; a mode gliding across 20 kHz fades out over one 32-sample update first.
- **Strike position.** Each mode is weighted by the uniform free-free mode shape
  `φ(x) = cosh βx + cos βx − σ (sinh βx + sin βx)` at the strike point (an approximation for undercut bars). At the
  centre the even modes, which have a node there, are silent.
- **Radiation voicing.** Even (antisymmetric) modes radiate less efficiently than odd ones; they start 6 dB lower. The
  value is a voicing constant, tuned against reference recordings in step 5 and recorded here.
- **Contact force.** Hertzian contact (exponent 3/2) scaling with a force `t e^(−t/θ)`, `θ = τ / 7`, lasting `10 θ`,
  for a contact time `τ = τ₀(Hardness) · v^(−1/5)`: harder and faster strikes are shorter and brighter. The pulse has no
  spectral nulls and rolls off as 1 / f², like a real contact. (A Gaussian, also null-free, was tried first and
  rejected in step 5: it rolls off so steeply that the default mallet barely reached the second partial.) The impulse
  is proportional to `v`, the mallet's momentum. Attack and Hardness change the brightness, not the level: each strike
  is scaled so the struck bar peaks as with the default contact at that velocity, predicted by a short simulation of the
  modes (with their decay) at 48 kHz or the engine's rate, whichever is coarser. The tube is left out: a harder mallet
  excites the fundamental less, so it swells the tube less, as on a real marimba. A contact coupled to the moving bar
  arrives with the modal-family work in Milestone 3.
- **Contact noise.** The mallet's "tock": a noise burst under the contact pulse's envelope, high-passed at 1 kHz and
  added to the output, louder for harder mallets (−22 dB re the body's peak at Hardness 0 rising to −10 dB at 100 %),
  growing as `v^1.5` (faster than the tone, so harder hits sound brighter), its energy independent of the contact's
  duration (a soft, long contact makes less noise, not more), and tilted by Tone like the modes. It gives every hit its
  own transient texture.
- **Damping.** Mode `n` decays at `α_n = α₁ (f_n / f₁)^p`, with `α₁` from Decay and `p = 1.8 − 1.4 m` from Material
  `m`: wood damps high modes strongly, metal rings evenly. No mode decays faster than T60 2 ms.
- **Output.** Mode velocity (flat across modes for an impulse) with the radiation voicing and Tone's tilt as output
  gains. The tube is a second single-mode `ModalBank` at the fundamental, fed by the bar's output, with Q 30, so its
  ring scales with pitch like a real resonator tube (T60 about 0.5 s at C3, 0.13 s at C5). Resonator's level moves per
  sample; at 0 the tube is cleared, so no stale ring returns when it is raised again.

| Control | Range and curve | Default |
| --- | --- | --- |
| Material | damping exponent `p = 1.8 − 1.4 m`: wood → metal | 20 % |
| Hardness | contact time `τ₀ = 6 ms · (1/24)^h` (6–0.25 ms) | 50 % (1.2 ms) |
| Position | strike point `x = 0.5 − 0.47 · position` (centre to near the end) | 25 % |
| Overtones | uniform bar → xylophone (1:3:6) → marimba (1:4:10); the editor marks Bar, Xylo and Marimba | 100 % |
| Resonator | tube level 0–100 %; skipped at 0 | 50 % |

| Shared control | Meaning in this model |
| --- | --- |
| Pitch | fundamental; useful range C2–C8 (MIDI 36–108, 65.4–4186 Hz) |
| Attack | contact softening: `τ × (1 + 4 a²)`, capped at 20 ms |
| Decay | fundamental T60 `50 ms · 400^d` (50 ms–20 s) at C4, scaled by `(f₁ / 261.6 Hz)^(−0.5)` so higher bars ring shorter, as real ones do (exponent a voicing constant tuned in step 5); the editor shows the T60 at the current pitch |
| Tone | spectral tilt `24 (t − 0.5)` dB/octave relative to the fundamental, as mode output gains |
| Drive | shared output-stage Drive |

- **Velocity:** through the contact pulse (shorter, brighter and stronger with `v`).
- **Variation (at 100 %), in draw order:** strike position ±0.04 (changes the balance of the upper partials, barely the
  fundamental), contact time ±10 % (brightness), strike force ±1 dB (through the contact law, so a stronger strike is
  also brighter), contact-noise level ±3 dB, then the contact-noise seed. Fixed: every mode's frequency and decay, and
  the onset time.
- **Note Off Damps:** Note Off moves every mode's T60 to at most 80 ms over 2 ms.
- **Calibration:** with defaults at C4, a full-velocity hit peaks at −6 dBFS ±1 dB at the engine output.
- **Idle:** inactive when every mode and the tube sleep and no pulse is pending.

**Factory preset targets:** Rosewood Marimba (defaults at C3), Xylophone (Overtones 50 %, Hardness 80 %, Material 30 %,
Resonator 20 %, C5), Vibraphone (Material 90 %, Decay 6 s, Resonator 40 %, F3), Glockenspiel (Material 100 %,
Overtones 0 %, Hardness 90 %, Resonator 0 %, C6).

## Milestone 1 Acceptance

Bounds come from the sheets above. Automated criteria run as `[flint]` tests (evidence under Status); A19 (step 11),
A21 visual, A22 listening and A23 hosts are measured or manual gates, open until run.

- **A1.** With one engine selected, the other's `process` count stays 0 through a MIDI sequence and preset loads during
  processing; no third engine appears during a Mode/Model preset change.
- **A2.** After a tail ends, including oversampling filter tails at every quality, every block is exactly `0.0f` and
  engine call counters stop.
- **A3.** No heap allocation or release during triggers, switches, preset loads or quality changes, counted through
  libmalloc's `malloc_logger` hook (`tests/flint/AllocationCounter.h`), which sees malloc, the typed allocators,
  `operator new` and JUCE's `HeapBlock`. Patching the default malloc zone counts nothing on macOS 27 (measured 3 October
  2026). Locks are not checked.
- **A4.** A note at offset *n* produces its first non-zero sample at *n* plus the active latency.
- **A5.** A Bar strike during a tail: the largest sample-to-sample step within ±5 ms of the strike is at most the
  isolated strike's largest onset step plus the tail's largest step there, plus 1e-6.
- **A6.** A Kick retrigger meets the same bound while the sweep restarts.
- **A7.** 32 strikes at 20 Hz at maximum Decay: the Kick peaks at most 6.5 dB above one strike, at the engine output;
  the Bar's loudest 10 ms RMS stays within 6.5 dB of one strike's at Resonator 0, and its peak within 8.5 dB with the
  default tube, a resonance that swells under sustained playing as a real one does (amended 3 October 2026: the per-mode
  cap bounds energy, while the peak also follows the modes' changing phases). On a long wooden Bar tail every strike
  still raises the band above 2 kHz by at least 6 dB within 5 ms (its attack is heard). A strike on a silent object
  takes the full strike (`strikeScale` returns exactly 1).
- **A8.** A one-octave Pitch change during a tail steps no more than the A5 bound and reaches the target within ±1 cent
  after 15 ms.
- **A9.** Note Off Damps shortens the tail to T60 60 ms (Kick) or 80 ms (Bar) ±20 % with no step above the A5 bound;
  off, Note Off changes nothing (bit-identical).
- **A10.** Decay is monotonic; the measured fundamental matches Pitch within ±1 cent over each model's useful range,
  and is clamped outside it; the calibration peaks hold; Attack and Hardness change the peak by less than 1 dB (the Bar
  at Resonator 0) while Hardness changes the brightness; the Bar's T60 at a fixed Decay falls with pitch.
- **A11.** Spectral centroid rises with Tone, including the Kick's click and the Bar's contact noise.
- **A12.** Drive 0 is bit-identical to the path without the Drive stage, for every Drive Type; Fold stays finite at
  every Drive value.
- **A13.** At 44.1 kHz, 96 kHz and the highest internal rate: pitch within ±1 cent, T60 within ±5 %; modes above
  20 kHz dropped.
- **A14.** Output always finite; mean over the last 100 ms of a long tail below 1e-4 (−80 dBFS).
- **A15.** Model, mode and switch-back transitions step at most the sum of both engines' largest steps in the window
  plus their peak magnitudes divided by the fade length in samples; preset loads reset old audio and still let a note
  sound in that callback.
- **A16.** (a) Variation 0 gives identical hits on a silent object. (b) At Variation 100 %, over 32 hits on a silent
  object: the low end does not move (Kick: fundamental within ±0.1 cent and the energy below 2 f₀ after 50 ms within
  ±0.1 dB of the Variation 0 hit; Bar: every mode's frequency and decay unchanged), while the transient does (the band
  above 2 kHz in the first 20 ms correlates below 0.9 between consecutive hits, with Kick Click 30 % and on the Bar, and
  its level stays within the sheets' ranges). Varied values follow the bell-shaped draw: more than half lie in the
  middle half of their range. (c) Rendering from bar 1 and from bar 3 gives bit-identical hits after bar 3 where
  nothing earlier rings. (d) New instances get different seeds; project save/restore keeps the seed, a failed restore
  leaves it, New Seed changes it. (e) Hits vary with the transport stopped.
- **A17.** Project round-trip through `PresetHost` with seed and `vektPresetSelection`; all-or-nothing presets; frozen
  parameter manifest and state fixture; booleans exact after fractional automation; host programs match the factory
  presets.
- **A18.** Quality lists and mappings from `OversamplingChoices.h`, defaults Off/Off; Tracking while playing, Offline
  when rendering offline; a change applies at the next block; latency correct; no allocation.
- **A19.** Release cost tool (`audio-lab-release`, run alone, 10 s screening then 30 s runs): idle Flint at most 2× an
  empty JUCE instrument in the same run; per-model active cost at 48 kHz recorded here.
- **A20.** After moving `LinearTptSvf`, Mono's reference renders match in Debug (including `svf-bandpass` and `k35`) and
  its SVF, nonlinear SVF and K35 cases pass.
- **A21 (visual).** At 1120×700 and 2240×1400: no overlaps; shared controls fixed across switches; five model slots
  rebind; unavailable entries marked; Pitch shows note, cents and frequency, the note actually played when clamped,
  and a dimmed arc outside the model's range; Drive Type under Drive; Velocity and Variation visible; Tracking/Offline,
  Note Off Damps and New Seed in Settings, with the active quality in the I/O strip.
- **A22 (listening).** 16th-note kicks, fast doubles, repeated Bar strikes and rolls on a long tail, a Pitch glide over
  a ringing tail, Note Off damping, Variation 0 to 100 %, each Drive Type, Bar Overtones uniform → xylophone → marimba
  with and without Resonator, against reference recordings.
- **A23 (hosts).** `scripts/pluginval-dev.sh flint` at strictness 10; `auval` after an approved install; an Ableton
  Live 12 Drum Rack smoke test including a quality change during playback and project reopen.
- **A24.** The manifest shows Mode, Model and Drive Type as fixed; Pitch's host text is note and cents.
- **A25.** At fixed Hardness the Bar's brightness rises with velocity and each of its first four partials grows with
  velocity; contact time falls as Hardness rises; sweeping Hardness, no partial dips more than 6 dB below both
  neighbouring steps (no spectral nulls).
- **A26.** Every Drive Type finite and stable; Fold's aliasing at 1x and 8x measured and recorded here.
- **A27.** Overtones 100 %: measured partials within ±5 cents of 1:4:10. Resonator 0 is bit-identical to the bar without
  the tube.
- **A28.** Glide, sweep envelope and modal decay at `maximumInternalSampleRate`: glide within ±1 cent, T60 within ±5 %
  of the 44.1 kHz value.
- **A29.** Beater and contact noise: level in the audible band within ±0.5 dB at 1x and 16x and at 44.1 and 96 kHz.
- **A30.** Kick classic sound: (a) the Classic preset matches the reference (the paper's figures; reference
  recordings when available) on fundamental, onset pitch rise, T60 and tone-stage response, within tolerances set in
  step 4 from those figures, and in an A/B listen (with A22); (b) Sweep 0, Click 0 and Body Shape 0 each add nothing
  (stage skipped, bit-identical).
- **A31.** Changing Mode in the editor sets the shared controls to that mode's start values in one undo step (undo
  restores the previous Mode and values); changing Model keeps them; model controls keep their own values.

## Known Limitations

- Looping a single bar repeats its hits exactly on every pass (song-position hashing).
- The allocation check does not detect locks.
- A Bar mode sleeps once its output, at the current Tone, falls below −120 dBFS. Upper modes that slept under a dark
  Tone stay silent if Tone is then raised over a long metal tail (review L4, 3 October 2026; not measured).
- The Bar's contact pulse is closed-form; a contact coupled to the moving bar arrives with the modal-family work.
- The tube is fed in series: it adds the resonator's ring but cannot take energy from the bar's fundamental as a
  coupled tube does.
- Offline quality above Tracking makes a bounce differ from playback, by the user's choice.
- Classic models follow their published circuit models with nominal values, not a particular unit, and depart from
  the circuit beyond the strike build-up cap. A per-instance "unit tolerance" is a possible later extension.
