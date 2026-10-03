# Flint Validation

Flint is a sample-free percussion synthesizer: one instrument per plugin instance, built into kits by the host (an
Ableton Drum Rack holds one Flint per pad). Its hierarchy is **Mode → Model → Sound**: Mode is the kind of instrument
(Kick, Snare, Mallet …), Model the synthesis method that makes it. This document is Flint's product contract and the
persisted plan for Milestone 1. Architectural decisions that freeze at release are recorded in
[ADR 0011](decisions/0011-flint-engine-architecture.md).

Flint is pre-release (3 October 2026): no project, preset or parameter format is binding until the first release.

## Status

| Step | Scope | Status |
| --- | --- | --- |
| 0 | This contract, the mode check, the Kick / Classic Analog and Mallet / Bar model sheets, ADR 0011 | Written 3 Oct 2026; **awaiting Thomas's approval of the sheets** |
| 1 | Move `LinearTptSvf` into `vekt_dsp`; Mono bit-identical | Planned |
| 2 | Scaffold `plugins/flint`; register Flint in `scripts/test-affected.sh` and `TestTagPolicy.cmake` | Planned |
| 3 | `FlintEngineHost`, output stage with Drive Type, stub engine, allocation counter | Planned |
| 4 | Kick / Classic Analog | Planned |
| 5 | Mallet / Bar | Planned |
| 6 | Hit hash and Variation; engine milestone (T2, review) | Planned |
| 7 | `flint::PluginProcessor`, quality, `PresetHost`, seed, wrappers, `pluginval-dev.sh` | Planned |
| 8 | Compat registration and fixture capture | Planned |
| 9 | Editor | Planned |
| 10 | Factory presets | Planned |
| 11 | Release cost tool (A19) | Planned |
| 12 | Script, packaging and skill registrations | Planned |
| 13 | Architecture, UI and workflow documentation | Planned |
| 14 | Final T3, listening, host checks, review | Planned |

**Next action:** Thomas approves the model sheets below; then step 1.

**Blockers:** none. Reference recordings for listening (A22) are Thomas's to supply.

## Product Principles

- **No voice allocator.** Each instance owns one persistent sound-making system; a new note strikes it again and never
  cuts its ringing tail. Retriggering either **restarts** (envelopes start over, analog models) or **re-excites** (adds
  energy to the ringing object, modal models).
- **Only the selected model uses CPU.** Unselected engines are never processed; a silent instance outputs exact zeros.
- **Six shared controls** keep their place for every Mode and Model: **Pitch, Attack, Decay, Tone, Drive, Level**, plus
  Fine. Each model implements them its own way. Below them sit four or five controls of the selected model.
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
- **Cymbal (Acoustic):** many modes; per-mode sleep must stay cheap at large counts (state as structure of arrays,
  inactive modes skipped in groups).
- **Ensemble:** memory for each model's maximum Count is reserved in `prepare`; Count changes never allocate.
- **Percussion exciter–resonator:** exciters are engine-internal; nothing beyond `Strike` crosses the interface.
- **Shaker / Particle:** one shot; Note Off is ignored unless Note Off Damps is on.

## Parameters

Parameter IDs, ranges and Mode/Model lists live in `include/vekt/flint/Parameters.h` only. Quality lists come from
`vekt/dsp/OversamplingChoices.h`.

| ID | Range | Default | Automatable |
| --- | --- | --- | --- |
| `flint.pitch` | MIDI note 12–108 (C0–C8), semitone steps; host text is the note name | 33 (A1, 55 Hz) | yes |
| `flint.fine` | ±50 cents | 0 | yes |
| `flint.attack`, `flint.decay`, `flint.tone`, `flint.drive` | 0–100 % | 30 %, 40 %, 50 %, 0 % | yes |
| `flint.level` | −48 to +12 dB | 0 dB | yes |
| `flint.velocity` | 0–100 % sensitivity | 100 % | yes |
| `flint.variation` | 0–100 % | 20 % | yes |
| `flint.noteOffDamps` | bool | off | yes |
| `flint.driveType` | Soft, Hard, Fold (grows at its end) | Soft | no |
| Tracking / Offline quality | `QualitySelection::makeTrackingParameter` / `makeOfflineParameter` | Off / Off | no |
| `flint.mode` | the nine modes (grows at its end) | Kick | no |
| `flint.kick.model` | Classic Analog, Punch Analog, Membrane, FM | Classic Analog | no |
| `flint.mallet.model` | Bar, Plate, Bell, FM | Bar | no |
| `flint.kick.analog.*` | sweep, sweepTime, click, bodyShape (Kick sheet) | | yes |
| `flint.mallet.bar.*` | material, hardness, position, tuning, resonator (Bar sheet) | | yes |

- Velocity: `v = 1 − s + s · velocity / 127` with sensitivity `s`; each model applies its own curve to `v`.
- Unimplemented modes and models play silence and are marked unavailable in the editor.
- The host shows Attack, Decay, Tone and Drive as percent; the editor shows the model's unit beneath (for example
  "1.2 s"). Pitch shows the note name in the host and note plus frequency in the editor ("D#2 · 77.8 Hz").
- Defaults are shared by every model; factory presets set their own values. Pitch above or below a model's useful
  range is clamped by the model.
- Flint tracks and renders Off by default, so a bounce matches playback (Mono renders offline at 4x FIR).

## Signal Path

- **`flint::PluginProcessor`** composes and owns no DSP: the parameter layout (read through
  `plugin_support::requireParameter`), MIDI splitting at event positions, the host playhead, the hit hash and the
  stopped-transport counter, `QualitySelection` with `OversamplingBank`, `PresetHost` and the seed.
- **`FlintEngineHost`** owns engine selection, switching, idle sleep and the output stage:
  - Selection changes in one step on the audio thread, only from a complete parameter snapshot; while a preset is being
    applied no switch happens. After a preset load old audio is reset and a note in the same callback still sounds.
  - A switch fades the old engine out and the new one in over 5 ms. Switching back during a fade reverses it without a
    reset. A newly activated engine's smoothing starts at the current values.
  - Sleep needs an inactive engine, output-stage tails below threshold and the downsampling filter's latency elapsed;
    on sleep all state is flushed and the output is exactly `0.0f`.
  - Output stage inside the oversampled region: Engine → Tone (where the model places it after the engine) → Drive.
    At host rate: Level → `DcBlocker` (5 Hz, flushed to zero below threshold) → safety clip.
- **Engine interface** (Flint-only, once per block segment, audio paths `noexcept`): `prepare`, `reset`,
  `activate(const Snapshot&)`, `trigger(Strike)`, `release(note)`, `process(left, right)`, `isActive()`, `energy()`.

### Shared stages

- **Drive:** `y = f(G x) · 0.5 / f(G · 0.5)` with input gain `G = 10^(36 d / 20)` for Drive `d` (0 → +36 dB), so a
  −6 dBFS peak keeps its level. Curves, each with first-order antiderivative anti-aliasing:
  Soft `tanh(x)`; Hard `clamp(x, −1, 1)`; Fold `sin(π x / 2)`. At Drive 0 the stage is skipped (bit-identical). The
  anti-aliasing delays the driven path by half a sample, so crossing zero while sounding crossfades the skipped and
  driven paths over 5 ms.
- **Safety clip:** identity for `|x| ≤ 0.891` (−1 dBFS), a smooth cubic knee above it, ceiling 1.0 (0 dBFS).
- **Energy-aware strikes:** each engine estimates the ringing amplitude `A` relative to one strike at the new strike's
  velocity and settings, `A = sqrt(E_ringing / E_single)`, and scales the new strike by `max(0, 1 − A / 2)`. A strike
  on a silent object is unscaled (bit-identical). For strikes repeating in phase every 50 ms the amplitude settles where
  `A (1 − r) = 1 − A / 2`, `r` the decay per interval: +5.3 dB at the Kick's longest decay, +5.7 dB at the Bar's.
- **Hit hash:** each hit's random values come from a hash of the instance seed, the hit's song position in beats
  rounded to 1/3840 beat, and its order among hits on the same sample. With the transport stopped or no playhead a
  per-instance counter replaces the position. The seed is drawn when the instance is created, saved in project
  metadata (not presets) and copied to an atomic for the audio thread; a preset load keeps it; New Seed redraws it.
- **Pitch glide:** pitch changes ramp linearly in semitones over 15 ms (`vekt::dsp::LinearRamp`, double); the ringing
  object follows. Host automation arrives once per block.
- **Precision:** per-sample state that steps toward a target or accumulates (glide, sweep envelope, resonator
  rotation, mode decay) is `double` and stays correct at `vekt::dsp::maximumInternalSampleRate` (3.072 MHz).

## Model Sheet: Kick / Classic Analog

**Character:** the 808-lineage bass drum: a pulse-struck resonance, close to a sine, from a short thud to a long sub
boom. Retrigger: **restart** the sweep while the resonator keeps ringing and is struck again.

**Topology.** Following Werner, Abel and Smith (DAFx 2014): a trigger/accent pulse shaper drives a resonant core (the
bridged-T network's second-order resonance, whose damping is set by Decay), followed by the output tone low-pass. The
core is a `vekt::dsp::LinearTptSvf` band-pass in double, chosen because its state stays continuous when its frequency
moves. The circuit's own onset pitch rise is taken from the paper's measurements in step 4 and is present at Sweep 0;
Sweep adds to it.

| Control | Range and curve | Default |
| --- | --- | --- |
| Sweep | extra onset pitch `36 s²` semitones (0–3 octaves) | 0 % |
| Sweep Time | sweep decay time constant `2 ms · 250^t` (2–500 ms) | 30 % (10.5 ms) |
| Click | high-passed (2-pole, 1.5 kHz) copy of the trigger pulse added to the output; 100 % peaks at the body's nominal peak; skipped at 0 | 10 % |
| Body Shape | `P · tanh(g x / P) / tanh(g)`, `g = 1 + 5 b²`, `P` the nominal body peak: sine-like at 0, rounded square at 100 %, level kept at the nominal peak | 0 % |

| Shared control | Meaning in this model |
| --- | --- |
| Pitch | resonator fundamental; useful range B0–C5 (MIDI 23–72, 30.9–523 Hz) |
| Attack | trigger pulse width `0.25 ms · 24^a` (0.25–6 ms); a wider pulse softens body onset and click |
| Decay | resonator T60 `40 ms · 200^d` (40 ms–8 s); `Q = π f₀ T60 / 6.91`, at least 0.5 |
| Tone | 2-pole low-pass at `150 Hz · 133^t` (150 Hz–20 kHz) after the engine |
| Drive | shared output-stage Drive |

- **Sweep restart:** on retrigger the sweep envelope rises to its top over 0.5 ms, then decays, so the resonator's
  frequency never steps and its output stays continuous.
- **Velocity:** pulse amplitude `v^1.5`; sweep depth `× (0.6 + 0.4 v)`; click level `× v²`.
- **Variation (at 100 %):** pitch ±3 cents, pulse amplitude ±0.5 dB, click level ±1.5 dB, sweep depth ±10 %; scales
  linearly with Variation.
- **Note Off Damps:** Note Off moves the resonator's T60 to 60 ms over 2 ms.
- **Calibration:** with defaults, a full-velocity hit peaks at −6 dBFS ±1 dB at the engine output.
- **Idle:** inactive when resonator energy and the sweep, pulse and click are all below −120 dBFS; the state is then
  zeroed.

**Factory preset targets:** Classic (defaults, 330 ms), Long Boom (Decay 3 s, Body Shape 30 %, Drive Soft 20 %),
Tight (Decay 120 ms, Sweep 3 semitones over 15 ms, Click 30 %), Distorted Sub (Decay 1.5 s, Drive Fold 35 %).

## Model Sheet: Mallet / Bar

**Character:** struck tuned bars from rosewood marimba and xylophone to metal vibraphone and glockenspiel, with an
optional tube resonator. Retrigger: **re-excite** the ringing bar.

**Topology.** A `ModalBank` of up to 12 modes (structure of arrays, double, complex one-pole resonators), driven by a
contact-force pulse at the strike position, optionally followed in series by a tube resonator at the fundamental.

- **Mode frequencies.** A uniform free-free bar has ratios 1, 2.756, 5.404, 8.933, 13.34, 18.64, 24.81, 31.87 …
  Tuning moves modes 2 and 3 in log-frequency, piecewise linearly: 0 % uniform (2.756, 5.404), 50 % xylophone (3, 6),
  100 % marimba (4, 10). Modes 4 and up keep the uniform spacing scaled by mode 3's factor. Modes above 20 kHz are
  dropped, at every sample rate.
- **Strike position.** Each mode is weighted by the uniform free-free mode shape
  `φ(x) = cosh βx + cos βx − σ (sinh βx + sin βx)` at the strike point (an approximation for undercut bars). At the
  centre the even modes, which have a node there, are silent.
- **Contact force.** Hertzian contact (exponent 3/2) as a closed-form pulse: a half-sine of duration
  `τ = τ₀(Hardness) · v^(−1/5)` and peak `∝ v^(6/5)`, so harder strikes are shorter and brighter. A contact model
  coupled to the moving bar is deferred to the modal-family work in Milestone 3.
- **Damping.** Mode `n` decays at `α_n = α₁ (f_n / f₁)^p`, with `α₁` from Decay and `p = 1.8 − 1.4 m` from Material
  `m`: wood damps high modes strongly, metal rings evenly. No mode decays faster than T60 2 ms.
- **Output.** Mode velocity (flat across modes for an impulse), weighted by Tone's tilt; the tube adds a resonance at the
  fundamental fed by the bar's output.

| Control | Range and curve | Default |
| --- | --- | --- |
| Material | damping exponent `p = 1.8 − 1.4 m`: wood → metal | 20 % |
| Hardness | contact time `τ₀ = 6 ms · (1/24)^h` (6–0.25 ms) | 50 % (1.2 ms) |
| Position | strike point `x = 0.5 − 0.47 · position` (centre to near the end) | 25 % |
| Tuning | uniform → xylophone (1:3:6) → marimba (1:4:10) | 100 % |
| Resonator | tube level 0–100 %; tube T60 300 ms at the fundamental; skipped at 0 | 50 % |

| Shared control | Meaning in this model |
| --- | --- |
| Pitch | fundamental; useful range C2–C8 (MIDI 36–108, 65.4–4186 Hz) |
| Attack | contact softening: `τ × (1 + 4 a²)` |
| Decay | fundamental T60 `50 ms · 400^d` (50 ms–20 s) |
| Tone | spectral tilt `24 (t − 0.5)` dB/octave relative to the fundamental, applied as mode output gains |
| Drive | shared output-stage Drive |

- **Velocity:** through the contact pulse (shorter and stronger with `v`).
- **Variation (at 100 %):** strike force ±1.5 dB, position ±0.03, contact time ±8 %.
- **Note Off Damps:** Note Off moves every mode's T60 to at most 80 ms over 2 ms.
- **Calibration:** with defaults at C4, a full-velocity hit peaks at −6 dBFS ±1 dB at the engine output.
- **Idle:** a mode sleeps when its energy is below −120 dBFS with no pulse pending; the engine is inactive when every
  mode and the tube sleep.

**Factory preset targets:** Rosewood Marimba (defaults at C3), Xylophone (Tuning 50 %, Hardness 80 %, Material 30 %,
Resonator 20 %, C5), Vibraphone (Material 90 %, Decay 6 s, Resonator 40 %, F3), Glockenspiel (Material 100 %,
Tuning 0 %, Hardness 90 %, Resonator 0 %, C6).

## Milestone 1 Acceptance

Planned; none run. Bounds come from the sheets above.

- **A1.** With one engine selected, the other's `process` count stays 0 through a MIDI sequence and preset loads during
  processing; no third engine appears during a Mode/Model preset change.
- **A2.** After a tail ends, including oversampling filter tails at every quality, every block is exactly `0.0f` and
  engine call counters stop.
- **A3.** No allocation during triggers, switches, preset loads or quality changes (`operator new` and the default
  zone's `malloc`, `calloc`, `realloc` and aligned allocation). Locks are not checked.
- **A4.** A note at offset *n* produces its first non-zero sample at *n* plus the active latency.
- **A5.** A Bar strike during a tail: the largest sample-to-sample step within ±5 ms of the strike is at most the
  isolated strike's largest onset step plus the tail's largest step there, plus 1e-6.
- **A6.** A Kick retrigger meets the same bound while the sweep restarts.
- **A7.** 32 strikes at 20 Hz at maximum Decay peak at most 6.5 dB above one strike's peak, at the engine output. A
  strike on a silent object is bit-identical with the scaling disabled.
- **A8.** A one-octave Pitch change during a tail steps no more than the A5 bound and reaches the target within ±1 cent
  after 15 ms.
- **A9.** Note Off Damps shortens the tail to T60 60 ms (Kick) or 80 ms (Bar) ±20 % with no step above the A5 bound;
  off, Note Off changes nothing (bit-identical).
- **A10.** Decay is monotonic; the measured fundamental matches note plus Fine within ±1 cent over each model's useful
  range, and is clamped outside it; the calibration peaks hold.
- **A11.** Spectral centroid rises with Tone.
- **A12.** Drive 0 is bit-identical to the path without the Drive stage, for every Drive Type.
- **A13.** At 44.1 kHz, 96 kHz and the highest internal rate: pitch within ±1 cent, T60 within ±5 %; modes above
  20 kHz dropped.
- **A14.** Output always finite; mean over the last 100 ms of a long tail below 1e-4 (−80 dBFS).
- **A15.** Model, mode and switch-back transitions step at most the sum of both engines' largest steps in the window
  plus their peak magnitudes divided by the fade length in samples; preset loads reset old audio and still let a note
  sound in that callback.
- **A16.** (a) Variation 0 gives identical hits on a silent object. (b) Variation 100 % stays within the sheets'
  limits (Kick ±3 cents and ±1 dB peak; Bar ±1.5 dB peak, centroid ±8 %). (c) Rendering from bar 1 and from bar 3
  gives bit-identical hits after bar 3 where nothing earlier rings. (d) New instances get different seeds; project
  save/restore keeps the seed, a failed restore leaves it, New Seed changes it. (e) Hits vary with the transport
  stopped.
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
  rebind; unavailable entries marked; Pitch shows note and frequency; Tracking/Offline in Settings with the active
  quality in the I/O strip.
- **A22 (listening).** 16th-note kicks, fast doubles, repeated Bar strikes, a Pitch glide over a ringing tail, Note Off
  damping, Variation 0 to 100 %, each Drive Type, Bar Tuning uniform → xylophone → marimba with and without Resonator,
  against reference recordings.
- **A23 (hosts).** `scripts/pluginval-dev.sh flint` at strictness 10; `auval -v aumu Flnt Vekt` after an approved
  install; an Ableton Live 12 Drum Rack smoke test including a quality change during playback and project reopen.
- **A24.** The manifest shows Mode, Model and Drive Type as fixed; Pitch's host text is a note name.
- **A25.** At fixed Hardness the Bar's centroid rises with velocity; contact time falls as Hardness rises.
- **A26.** Every Drive Type finite and stable; Fold's aliasing at 1x and 8x measured and recorded here.
- **A27.** Tuning 100 %: measured partials within ±5 cents of 1:4:10. Resonator 0 is bit-identical to the bar without
  the tube.
- **A28.** Glide, sweep envelope and modal decay at `maximumInternalSampleRate`: glide within ±1 cent, T60 within ±5 %
  of the 44.1 kHz value.

## Known Limitations

- Looping a single bar repeats its hits exactly on every pass (song-position hashing).
- The allocation check does not detect locks.
- The Bar's contact pulse is closed-form; a contact coupled to the moving bar arrives with the modal-family work.
- Offline quality above Tracking makes a bounce differ from playback, by the user's choice.
