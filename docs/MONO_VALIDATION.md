# Mono Sound Engine Development

**Mono quality scope revision (27 September 2026):** The selectable legacy
quality and both planned candidate playback/offline ranges end at 8x
(1x/2x/4x/8x). Previously proposed offline 16x, host export blocking and
16x acceptance/listening requirements in historical sections below are
superseded. 16x/32x offline reference calculations remain diagnostic tools,
not selectable product modes. Stored 16x (index 4) project states are rejected
without changing live state, not silently mapped to 8x; indices 0–3 are stable.
This product change responds to CPU concern, **not** a measured CPU verdict
from short diagnostic probes. The remaining modes still need validation.

## Target And Scope

Target a well-maintained Minimoog-style bass/lead core with modern extensions:
defined bass, prompt contours, smooth resonant sweeps, progressive overload, and
subtle independent oscillator movement. Polyphony, unison and stereo are useful
extensions, not substitutes for a convincing dry mono signal path.

Hardware captures are not required. Published circuit models, analytical checks,
numerical convergence and documented listening will guide development. Neither
passing these tests nor using a zero-delay-feedback algorithm establishes accuracy
to a particular Minimoog. No hardware-accuracy claim is made.

Mono is pre-alpha and has no compatibility obligation. Existing project states,
presets, parameter identifiers, parameter order, control mappings, DSP behavior and
fixture output may be invalidated or replaced when they conflict with the selected
architecture. Factory presets will be retuned for the completed engine. Legacy
behavior is retained only when independently justified by the product target, not
for compatibility. Shared behavior and other products remain outside this work.

This document distinguishes implemented development contracts from the final engine
contract. An implemented contract may be replaced deliberately by a later phase;
tests and fixtures then change with the documented decision rather than preserving
interim behavior.

## Frozen Product Direction For Ladder Validation

Mono follows a hybrid direction: preserve the core classic ladder and resonance
identity while treating compensation, quality, articulation and modulation as
deliberate modern product features. The following decisions are frozen for ladder
validation even though ADR 0005 remains Proposed:

- The authoritative ladder output is the raw fourth-stage output. Drive raises the
  actual input to the nonlinear ladder without implicit output normalization.
- Optional drive compensation is a separate, bypassable post-ladder sound-design
  feature. It is not part of the ladder equations or feedback loop.
- Q compensation remains separate, optional, default-off and post-ladder. Any future
  output-feedback tap precedes both compensation stages.
- Normalized resonance `1.0` is the nominal self-oscillation boundary. Threshold,
  startup, frequency, amplitude and ringdown are measured rather than inferred solely
  from the linearized equations.
- Static and audio-rate cutoff, resonance and drive modulation are required ladder
  validation scenarios.
- The interim legacy engine exposes one 1x/2x/4x/8x/16x GUI setting with
  1x default and does not restrict 16x during playback. Revised ADR 0001
  plans separate Playback Quality (1x/2x/4x/8x, default 1x) and Offline
  Render Quality (follows playback by default, explicitly overridable to
  1x/2x/4x/8x/16x) for the candidate.
  Historical 16x probes do not qualify offline-only behavior. No candidate
  quality path is accepted.
- Last-bounded-iterate solver behavior is not accepted merely because it is
  deterministic. The supported matrix must demonstrate zero fallback incidence or a
  continuous replacement must be specified and validated.

Classic oscillator-3/noise wheel modulation and final monophonic articulation remain
assigned to their later phases. They are neither required nor excluded by the ladder
contract. Mixer/VCA nonlinearity, a dedicated LFO and output feedback are also deferred.
These deferred decisions do not permit production ladder integration before ADR 0005
is accepted.

## Implemented Contracts

### Oscillator Ranges

The range multiplier now follows its footage label. At MIDI note 69, with neutral
octave/semitone/fine tuning and drift disabled:

| Range | Fundamental |
| --- | --- |
| 16' | 220 Hz |
| 8' | 440 Hz |
| 4' | 880 Hz |
| 2' | 1760 Hz |
| 1' | 3520 Hz |

Previously every non-neutral range ran in the opposite direction. Presets retain
their labeled range choices, not their erroneous previous pitches.

### Optional Q Compensation

`filterQCompensation` is an automatable sound parameter, exposed by the filter
panel's **Q Compensation** checkbox. The parameter, startup preset and all factory
presets default to off. Projects and presets recall the checkbox state.

The old input-side compensation has been removed. Off preserves the ladder's
natural loss of low-frequency passband level as resonance rises. On applies gain
after the current nonlinear filter and linear VCA/declick stage. That gain is
outside the resonance loop and does not modify oscillator, filter, envelope, or
continuity state. Any future nonlinear VCA/output stage must precede compensation;
any future output-feedback tap must also precede compensation.

For normalized resonance `r`, the provisional target gain is:

```text
off: 1
on:  min(10^(12/20), 1 + 4 * r^0.72)
```

This bounded law is a development choice, not a calibrated hardware measurement.
It is unity at zero resonance and capped at +12 dB. Gain changes ramp linearly over
20 ms at the effective processing rate, without resetting notes or adding latency.
The first activation after preparation initializes directly to the selected gain.

Compensation raises the resonant peak and noise along with the fundamental. It is
not an EQ, limiter, AGC, or a guarantee of constant loudness. Reduce monitoring gain
before testing high resonance, many voices, or heavy drive. Full nonlinear-path
headroom characterization remains pending.

The current development increment uses sound preset schema 4 and project schema 3.
These versions are not compatibility commitments. Mono supports only its current
schemas at each development milestone: earlier project states and presets are
rejected rather than migrated. The final parameter set may remove, rename, reorder
or replace current parameters, followed by one final pre-alpha schema reset and a
complete factory-preset rebuild. Boolean host values are currently resynchronized
on current-schema project restore to avoid JUCE retaining fractional values for an
already-snapped state.

### Mono Performance

- Glide Off snaps note pitch irrespective of the time knob.
- Always glides from the remembered note, including after its voice falls silent.
  The first note after reset has no prior pitch and starts directly.
- Legato glides only when another key remains held, independently of whether the
  envelope is retriggered. An audible release tail alone is not a held gate.
- Mono retriggers on each note. Mono Legato restarts on a new gate, including a
  new note during the previous release tail, but retains contours for overlapping
  notes. Retrigger still starts from the current envelope level, not forced zero.
- Held-key return restores the original key's velocity and its amplitude/filter
  response, rather than substituting full velocity.
- Pitch bend is applied independently of note glide time. Dedicated bend smoothing
  is not introduced by this increment.

Last-note priority and existing sustain behavior remain. A classic low-note
priority option and more complete mode-transition behavior still need assessment.

## Automated Evidence

The existing CMake/Catch2 `vekt_dsp_tests` target contains the checks. New tests use
explicit parameter defaults instead of inheriting the constructor's factory sound.

- Measured fundamentals for all footage choices on all three oscillators.
- Uncompensated low-frequency attenuation as resonance increases.
- Compensated and uncompensated outputs differ only by the specified scalar gain
  at 44.1/48/96 kHz, both quality modes, zero/maximum drive, and self-oscillation.
- Held-note compensation switching follows the 20 ms ramp and returns to the
  unchanged uncompensated output. Latency is unaffected.
- Parameter defaults, all factory preset defaults, boolean project recall,
  obsolete-schema rejection without mutation, checkbox attachment, and editor
  bounds.
- Glide truth table across both mono modes, overlapping notes, release tails and
  silence; original held-key velocity; re-gating during release; bend independence.
- Existing note-transition continuity and self-oscillation checks remain in place.

These are correctness and selected numerical checks, not listening results. The
existing suite passed before the implementation began. The range and glide tests
were observed failing before their fixes. No pre-change WAV archive or hardware
comparison has been captured in this increment.

## Deterministic Audio Lab Fixtures

`VektMonoRender` drives the product-local `MonoVoice` directly. It applies note
and control events at absolute sample positions, independently of render block
boundaries, and seeds the voice's oscillator phase, drift and noise generator
explicitly. It does not duplicate oscillator, contour, ladder, VCA or panning DSP,
and it does not add development controls to plugin state or presets.

Build and render the fixed fixture set in Release mode with:

```sh
./scripts/render-mono-report.sh /tmp/vekt-mono-render
```

The script writes a 32-bit stereo WAV and an indented JSON report for each fixture:

- `filter-sweep`: one held note, white noise and drift with cutoff changes at
  0.5 s and 1.0 s, a resonance change at 1.25 s, and note-off at 1.6 s.
- `envelope`: an exact amp-release control change at 0.05 s, note-on at 0.1 s,
  and note-off at 0.9 s. The control precedes note-on because the current JUCE ADSR
  snapshots its parameters when the contour starts; live contour edits remain part
  of the pending contour assessment.

Each report records the sample rate, block size, seed, initial settings, complete
event timeline, stereo RMS/peak/DC/first-difference RMS, and named measurement
windows. First-difference RMS is a deterministic high-frequency-content proxy for
the filter fixture, not a cutoff-frequency estimate. The envelope report uses a
5 ms one-pole magnitude follower and records 10%-to-90% attack and release-to-10%
times. Those values include oscillator phase, the existing 3 ms allocation fade,
filter response and the measurement follower; they are calibrated regression
measurements of the current output, not direct readings of the ADSR state and not
hardware accuracy claims.

Tests cover exact event positions, fixed-seed repeatability, sample-for-sample
invariance across render block sizes, expected filter-window separation, envelope
threshold extraction, and readable WAV/JSON output. The renderer intentionally
models one voice. Processor MIDI allocation, oversampling and processor/direct-engine
parity remain covered by the processor test suite rather than duplicated here.

### Development Ladder Comparison

The same render script also builds `VektLadderPrototype` and writes
`ladder-candidate.wav` and `ladder-candidate.json`. The production delayed-feedback
ladder is extracted into a reusable product-local class without changing its
arithmetic, state lifecycle or sound; pre/post extraction fixture WAV and JSON
files are byte-identical. The candidate remains isolated and is not selected by
`MonoVoice`, the processor, plugin parameters, presets or saved state. It combines
the four nonlinear one-pole ladder structure
described by Antti Huovilainen in *Non-Linear Digital Implementation of the Moog
Ladder Filter* (DAFx-04) with trapezoidal/TPT integrators and a bounded Newton solve
for the instantaneous global feedback loop. Vadim Zavalishin's *The Art of VA
Filter Design* is the TPT and delay-free-loop reference. This combination is a
development interpretation, not an assertion that either source specifies this
exact implementation and not a hardware-accuracy claim.

The comparison report applies the same low-level gain/phase frequencies, -3 dB
interpolation, input levels and impulse thresholds to the current and candidate
models. It records onset, peak, extinction status, estimated frequency and
non-finite samples for both, plus maximum Newton iterations, residuals and
unconverged samples for the candidate. At the fixed 0.98 resonance probe the
current core reaches sustained self-oscillation rather than extinguishing; the
report records that outcome explicitly instead of treating it as a finite ringdown.
The report also includes per-frequency gain/phase deltas, per-level fundamental
and output-component comparisons, ringdown deltas, and a measurement-only candidate
output scale derived from the non-resonant 125 Hz low-level response. This scale
does not alter either implementation or the rendered candidate WAV. At high
resonance the current core's autonomous oscillation means components measured
relative to a very small input are not conventional harmonic-distortion ratios;
the report labels them as output components and records this limitation.
At 48 kHz with a 1 kHz cutoff control, the fixed low-level probe measures about
372.91 Hz at -3 dB for the current core and 403.75 Hz for the candidate. Matching
their 125 Hz output requires only -0.034 dB of candidate measurement gain. After
that normalization, the candidate remains about 0.43 dB higher at 500 Hz, 1.07 dB
higher at 1 kHz and 1.69 dB higher at 4 kHz, so the response difference is not a
simple output-level offset. At the fixed 0.98 resonance probe the current core
sustains approximately 1.002 kHz oscillation, while the candidate decays and its
onset-window estimate is approximately 1.032-1.041 kHz.
The cutoff-mapping matrix additionally probes 250 Hz, 1 kHz and 4 kHz controls at
44.1, 48 and 96 kHz. Proportional candidate-control scale estimates range from
approximately 0.773 to 0.986, a spread of about 422 cents. Re-probing a single
geometric-mean scale of approximately 0.9075 leaves up to about 273 cents of error.
These values demonstrate that the implementations differ; they do not establish
that either response is correct. The legacy ladder is not an authoritative cutoff
target, so neither a constant multiplier nor a frequency/rate-dependent remapping
to it is an acceptance objective. Final calibration must come from the selected
continuous-time model, its analytical response and converged nonlinear references.
The old/new matrix remains useful only for regression context, level separation and
listening comparisons.
The development candidate now uses double-precision state with at most 16 bracketed
global-feedback iterations and 24 bracketed iterations per nonlinear stage. Tests require
silence preservation, finite output, solver convergence for the fixed probes,
sample-for-sample block-size invariance, expected low-pass/phase behavior and
readable WAV/JSON output.

The candidate report now describes the frozen ladder contract, planned supported
matrix and provisional acceptance limits. It also records a coherent two-tone IMD
probe and a stopband diagnostic that reports both relative gain error and absolute
output error. The latter prevents large decibel ratios on vanishing signals from being
mistaken automatically for audible or structurally significant error. These additions
are representative evidence only: the report marks matrix completion as incomplete.

### Planned Supported Ladder Matrix

- Host rates: 44.1, 48, 88.2, 96 and 192 kHz.
- Cutoff: the 10 Hz floor, representative low/mid positions, one-quarter of the active
  rate and the `0.45 * rate` ceiling.
- Resonance: 0, 0.5, 0.85, 0.95, 0.98 and 1.0.
- Input peaks: `1e-6`, `0.001`, `0.05`, `0.5`, `1.0` and `4.0` before drive.
- Drive: 0, 6, 12 and 24 dB.
- Block sizes: 1, 16, 31, 32, 127, 128 and 257 samples.
- Stimuli: silence, DC, positive/negative impulses, sine, sweep, deterministic noise,
  two-tone and modulated tone.
- Controls: static; independently modulated cutoff, resonance and drive; and combined
  modulation.
- Planned candidate controls: Playback Quality 1x (Off), 2x IIR, 4x/8x FIR;
  Offline Render Quality additionally offers 16x FIR, eligible only when
  offline processing is verified. Use still higher offline-reference rates
  for ground-truth comparisons. Interim legacy still permits 16x playback;
  the split controls and offline restriction are not implemented.

The matrix is complete only when every supported production path has explicit
analytical/nonlinear-reference, stability, determinism, fallback, aliasing and Release
cost evidence. A smaller representative report is not a substitute for that gate.

The static development solver matrix now passes zero unconverged samples for its
enumerated 256-sample probes, including the former 44.1 kHz, 10 Hz, resonance 0.5,
input peak 4.0, +24 dB failure. This does not establish fallback freedom for all
stimuli, modulation, quality modes or long durations. A separate representative 48 kHz
modulation probe passes finite output and sample-exact invariance for cutoff-only,
resonance-only, drive-only and combined modulation across the listed block sizes.

An additional development-only 8,192-sample-per-case solver gate spans all five host
rates, cutoff floor/1 kHz/ceiling, resonance 0/0.98/1, drive 0/24 dB, and nine
stimuli: silence, positive DC, both impulse polarities, sine, sweep, seeded noise,
two-tone and a simultaneously modulated tone. Its two identical renders are checked
bit-for-bit, along with finite/bounded output, zero unconverged samples and residual
at most `2e-7`. This is 810 cases (6,635,520 processed samples per render), not the
full cross product of all listed amplitudes, cutoffs, controls, durations and quality
paths. The modulated case changes cutoff, resonance and drive together; the
independent modes are covered below at representative levels through development
Off/2x/4x paths, but the complete production-path matrix remains open.

The development candidate has now also been exercised through the repository's
prepared `OversamplingBank<float>` upsample/ladder/downsample paths: Off, 2x IIR,
2x FIR, 4x IIR and 4x FIR at all five host rates. A 4,096-host-sample probe
of independent cutoff, resonance and drive modulation plus combined modulation
at 1/31/128-sample blocks passed finite output, bit-exact block independence,
zero solver fallback and residual at most
`2e-7`. An additional 512-host-sample alternating `+4/-4`, +24 dB overload
probe passed 225 rate/quality/cutoff/resonance combinations (10 Hz, 1 kHz and
`0.45 * internalRate` cutoff; resonance 0, 0.98 and 1). These are development
quality-path tests using the real up/downsamplers, **not** a production processor
benchmark or proof of alias rejection. They do not cover the full amplitude/drive
matrix, long durations, or the complete production path.

A separate development regression adds the selectable 8x and 16x FIR
paths at all five host rates. For 512 host samples per case, it compares
bit-exact output at 1- and 127-sample blocks on a 0.5-peak, 7 kHz tone
with simultaneously modulated cutoff (1,200 ± 800 Hz at 37 Hz),
resonance (0.85 ± 0.15 at 23 Hz) and drive (+12 ± 6 dB at 41 Hz),
and on both sample-alternating +4/-4 and 64-host-sample +4/-4
plateaus at +24 dB drive, resonance 1.0 and `0.45 * internalRate`
cutoff. All 30 rate/factor/stimulus combinations have finite, bit-exact
host output, zero solver fallback/non-finite
samples and residual at most `2e-7`; the plateau case also verifies
that the upsampled ladder input reaches more than 3.0 peak in each
rate/factor/block configuration. This does **not** prove long-run
stability or cover full modulation cycles at every host rate;
it also does not establish allocation safety, alias rejection, or
complete candidate processor real-time feasibility for these paths.

A single coherent 48 kHz alias probe passes through those same five paths at a
7 kHz, 0.5-peak sine, 10 kHz cutoff, resonance 0.5 and +12 dB drive. With 0.25
seconds settling and 0.25 seconds measurement, the fifth harmonic (35 kHz)
folds to 13 kHz: measured alias/fundamental levels are approximately `-39.9 dBc`
Off, `-127.1 dBc` 2x IIR, `-139.2 dBc` 2x FIR, `-130.3 dBc` 4x IIR and
`-137.6 dBc` 4x FIR. The regression requires each oversampled path to reduce
this one folded component relative to Off. Other aliased harmonics, spectral
windows, sample rates, modulation, overload and the complete production signal
path have **not** been accepted against the proposed alias targets.

The same folded-fifth-only measurement was extended to all five host rates, at
fundamental `7/48 * hostRate`, cutoff `10/48 * hostRate`, input peak 0.5,
drive 12/24 dB and resonance 0.5/0.98 (20 operating points per quality path).
Each oversampled path reduced this component relative to Off and passed the
*proposed single-component* ceilings of `-60 dBc` at 2x and `-80 dBc` at 4x.
The 4,800-host-sample measurement window is coherent at all five rates. This
does **not** establish maximum alias-spur limits over the full spectrum, alias
rejection for other stimuli, or product approval of either threshold.

An expanded coherent-bin audit of that same rate/drive/resonance/quality
matrix also projects the folded seventh (`1/48 * hostRate`) and ninth
(`15/48 * hostRate`) harmonics. At +12 dB drive, the worst oversampled
seventh/ninth across the matrix are approximately `-116/-134 dBc`.
At +24 dB, resonance 0.98, the 48 kHz seventh is about `-51.3` and
`-52.4 dBc` at 2x IIR/FIR, and `-75.5` and `-76.0 dBc` at 4x IIR/FIR;
the corresponding ninth is `-68.7`, `-70.2`, `-92.0` and `-92.3 dBc`.
The fifth at that setting is about `-65.4`, `-66.3`, `-90.0` and
`-90.5 dBc`: **passing the fifth does not bound the seventh**.
The +12 dB proposal below must not be misapplied to +24 dB, and a
high-drive alias policy must be reviewed with full-spectrum measurements
and listening before acceptance. Coherent projection of three bins is
not a maximum-spur test or a high-rate reference comparison.

A separate 48 kHz settled single-tone **host-band coherent-bin scan**
(7 kHz sine, 0.5 input peak, 10 kHz cutoff, 0.1 s settling and
0.1 s measurement) covers Off/2x/4x IIR/FIR, resonance 0.5/0.98 and
drive +12/+24 dB. It checks the 1–23 kHz bins, excluding the intentional
in-band 7, 14 and 21 kHz harmonics. At +12 dB and resonance 0.98,
the strongest other bin at 2x IIR/FIR is 5 kHz near `-48.6 dBc`,
despite the passing folded-fifth measurement; at 4x it is 3 kHz
near `-106 dBc`. At +24 dB, resonance 0.98, the 2x 5 kHz bin
is near `-24.3 dBc` and the 4x 3 kHz bin near `-42.3 dBc`.
These are **unattributed non-harmonic coherent spurs**, not proven aliases:
an aligned, adequately converged higher-rate same-input comparison is
needed to identify their origin. The +12 dB 2x result exceeds the
proposed `-60 dBc` alias target *if* confirmed as alias, so the proposed
target is **not demonstrated** by this operating point. Neither the
three-bin projection nor this scan passes a complete spectral gate.

A targeted **same-input, raw-tap** 48 kHz offline-model comparison of
the +12 dB, resonance-0.98 case at 10 kHz cutoff measures the 5 kHz
component at about `-87.62`, `-87.99`, `-88.08` and `-88.10 dBc`
with 16, 32, 64 and 128 pole-preserving substeps (3 kHz:
`-127.4..-128.5 dBc`). The
2x host-band 5 kHz result above is about **39 dB higher** than this
128-substep reference bin. The offline 5 kHz bin still changes by
approximately `0.023 dB` from 64 to 128 substeps; this comparison does
not establish a converged reference, latency-aligned full-band
candidate/reference difference, or the physical source of the spur.
It does establish that the 2x result must not be counted as an
expected component of this provisional offline-model comparison.

An internal-tap probe of the same input through the FIR paths confirms
the 2x 5 kHz component is already `-48.59 dBc` **before** downsampling
and is unchanged to `0.1 dB` at the host tap; the upsampled input's
5 kHz component is only `1.27e-9` peak. At 4x, the 3 kHz component is
approximately `-106.19 dBc` pre-downsampling and `-106.20 dBc` at the
host tap, versus `1.33e-9` peak at the upsampled input. The factor-
dependent fold (2x 5 kHz, 4x 3 kHz, much lower at 8x/16x) is consistent
with internal nonlinear aliasing: at 2x, 13 times the 7 kHz fundamental
is 91 kHz and folds to 5 kHz at the 96 kHz internal rate; at 4x,
27 times 7 kHz is 189 kHz and folds to 3 kHz at 192 kHz. This bin
arithmetic is a hypothesis about the generating harmonic, **not** an
unaliased measurement of that harmonic or a complete alias limit.
An additional coherent internal-tap regression for this 48 kHz stimulus
measures the 2x FIR 5 kHz fold at `-48.59 dBc` but the **unfolded** 91 kHz
component at only `-139.14 dBc` in a 16x FIR internal render. Because
the two paths solve the nonlinear model at different rates (and have
different upsampling filters), the 16x amplitude is not the unaliased
amplitude that the 2x discrete solver would have generated. The arithmetic
identifies a possible folding frequency, not a verified physical source;
the 2x discrepancy still requires mitigation and an aligned converged
reference comparison before approval.

To isolate the internal integration from the oversampling filter, a
development probe feeds **identical float samples from each 2x IIR and
2x FIR upsampler** to the candidate and to a double-precision offline
reference prepared at the same 96 kHz internal rate, with 1/4/16/32
substeps per internal input sample. For both filters, the coherent internal
5 kHz bin is approximately `-48.59 dBc` for the candidate and one-step
reference, then `-78.86`, `-80.36` and `-80.43 dBc` for 4/16/32 substeps.
The 16-to-32 difference is about `0.075 dB` at that bin; this is not a
full-render convergence proof.
All measured reference steps converge with residual at most `1e-13`.
The same-input result implicates the one-step, 96 kHz discretization as a
substantial contributor to this spur, rather than solely float precision
or the downsampler. It does **not** make the substepped reference a viable
real-time 2x implementation: internal substeps increase solver cost and
require full-band, latency, modulation, other-rate and CPU validation.
It also does not replace a latency-aligned host-rate reference comparison.

The same 48 kHz, 7 kHz stimulus is now also measured **after the real 2x
bank's downsampling** with separately prepared, identically configured
IIR/FIR banks for each solver. At the host tap the candidate's 5 kHz bin
is `-48.59 dBc` for either filter, versus approximately `-78.86 dBc`
with four reference substeps and `-80.43 dBc` with 32. The improved bin
survives downsampling; the downsampler does not repair the one-step
discrepancy. These are offline double-reference measurements for a single
static tone, not a CPU-feasible real-time change or a full-band pass.

A 48 kHz **settled coherent host-band scan**, using the same 7 kHz,
0.5-peak input and 10 kHz cutoff, now examines all 1–23 kHz bins after
2x IIR/FIR downsampling at resonance 0.98. It excludes the intended
7/14/21 kHz in-band harmonics. The strongest remaining bin is 5 kHz
for every tested path: at +12 dB drive the one-step candidate, four-step
reference and 16-step reference measure about `-48.59`, `-78.86` and
`-80.36 dBc`; at +24 dB they measure about `-24.27`, `-59.28` and
`-60.09 dBc`. Both filters agree within roughly 0.01 dB. These are
**single-tone coherent bins**, not a continuous full-band maximum or an
approved overload ceiling. The +24 dB four-step result must not be
reported as meeting a `-60 dBc` ceiling, and the +12 dB proposal below
must not silently be extended to overload. No real-time substepped
candidate exists yet, and no CPU/latency or listening gate has passed.

A separate fixed-absolute-frequency probe retains a 7 kHz input and
10 kHz cutoff across 44.1/48/88.2/96/192 kHz host rates, with 2x IIR,
resonance 0.98 and +12 dB drive. After 0.1 s settling, it scans every
100 Hz coherent host bin below Nyquist for 0.1 s, excluding direct
in-band multiples of 7 kHz. The largest remaining bins (candidate →
four-substep **offline** reference) are: 44.1 kHz: 2.8 kHz,
`-46.92 → -76.11 dBc`; 48 kHz: 5 kHz,
`-48.59 → -78.86 dBc`; 88.2 kHz: 1.4 kHz at `-98.21 dBc` →
2.8 kHz at `-130.15 dBc`; 96 kHz: **47 kHz**,
`-95.61 → -93.24 dBc`; 192 kHz: 1 kHz at `-133.69 dBc` →
95 kHz at `-152.57 dBc`. In particular, at 96 kHz the largest
measured bin becomes about **2.37 dB higher** with four substeps;
substepping is not a monotonic reduction of every non-harmonic bin.

An additional 96 kHz 47 kHz host-bin probe finds `-95.61 dBc` for the
one-step candidate and `-93.24`, `-93.11`, `-93.09 dBc` for 4/16/32
offline substeps. The 16-to-32 change is about `0.015 dB` at this bin;
the higher level is not peculiar to just four steps. This does not
establish convergence of the full output or attribute the spur to a
particular physical mechanism.

At the **pre-downsampling** 192 kHz internal tap, the corresponding
49 kHz seventh-harmonic bin is approximately `-85.10`, `-82.73`,
`-82.58`, `-82.57 dBc` at 0/4/16/32 substeps. The internal 47 kHz
bin is below `-155 dBc` in all four renders. The host 47 kHz bin
follows the internal 49 kHz bin within `0.1 dB` of the candidate-to-
four-step change, with approximately `10.5 dB` difference in their
fundamental-relative levels after decimation. This supports **folding
of the measured internal 49 kHz component** in the real 2x IIR bank;
it does not establish why that component changes with integration
substeps, or a converged full-render reference.

The 44.1 kHz 2.8 kHz and 48 kHz 5 kHz locations are consistent with
the 91 kHz 13th harmonic folding at their respective 2x internal rates;
the 96 kHz 47 kHz location is consistent with folding of the 49 kHz
seventh harmonic. This frequency arithmetic does not establish the
unaliased generating amplitudes. This scan covers one static input and
coherent bins only: it is not a continuous-spectrum, modulation, or
worst-case-frequency product gate.

A different five-rate normalized probe scales input to `7/48 * rate`
and cutoff to `10/48 * rate`; for 2x IIR, resonance 0.5/0.98 and
drive +12/+24 dB, the largest scanned coherent non-harmonic bin is 5
in every tested case and is lower with four offline substeps. The
nearly identical dBc values at each rate result from this normalized
stimulus, **not** independent absolute-frequency validation.

The 48 kHz +12 dB, resonance-0.98
host-band scan's strongest non-harmonic spur is `-124.1 dBc` at 8x FIR
and `-124.2 dBc` at 16x FIR; at +24 dB the corresponding strongest
spurs are `-81.8` and `-110.8 dBc`. Higher oversampling is a possible
quality-policy change, not an approved fix: independent absolute-frequency
coverage, production CPU, and listening remain unmeasured. The currently supported
production quality choices are Off and 2x IIR on the **legacy** ladder.

The normalized coherent-bin scan now also checks 8x and 16x FIR at all
five planned host rates, with input frequency `7/48 * hostRate`, cutoff
`10/48 * hostRate`, resonance 0.5/0.98 and drive +12/+24 dB. This is
the **same normalized stimulus** at each rate, not independent absolute-
frequency coverage. For that stimulus only, the worst non-harmonic bin
through 8x stays below `-110 dBc` at +12 dB and `-75 dBc` at +24 dB;
through 16x it stays below `-110 dBc` at +12 dB and `-100 dBc` at
+24 dB. These are development-regression bounds, not approved product
limits. A candidate quality replacement is permitted for investigation,
subject to CPU, latency, matrix, listening and explicit approval. No
quality choice has yet passed those gates.

A **Release-only candidate-path cost probe** is available as
`VektLadderCost rate block_size ladder_count factor` from the Audio Lab
Release build. `ladder_count` counts *separately prepared candidate
filters*, not synth voices. It measures one mono oversampling bank plus
8 or 16 such filters processing the same 330 Hz 0.25-peak sine,
1 kHz cutoff, 0.85 resonance, +12 dB drive, 0.5 seconds of offline
callbacks per run and no warmup. It reports median, nearest-rank 99.9th
percentile, max, and count of calls exceeding a **simulated** block
deadline; this is not an actual missed audio callback count. No voice
oscillators, envelopes, stereo paths, parameter automation, transport,
host, callback allocation instrumentation, or complete processor are
included. It uses `std::chrono::steady_clock` on an Apple M1 Pro
MacBookPro18,1; host scheduling and other system activity may affect
tails. The checksum prevents treating an empty render as a valid cost.
The results are not an approved CPU budget or a 30-second test.

After the product selected the local **Apple M1 Pro (MacBookPro18,1,
32 GB)** as a physical CPU measurement target, a separate Release
`VektMonoProcessorCost rate block_size voices factor seconds` tool was
added. It times the **existing legacy** `PluginProcessor::processBlock`
with 8/12/16 held polyphonic notes, oscillator/envelope/filter/VCA,
stereo oversampling, output meter, 1 kHz cutoff, 85% resonance and
+12 dB drive; it reports processor latency and a nonzero-output check.
Parameters and note-on happen outside the timed loop, followed by at
least 100 ms of audio-time warm-up at every block size. It does not
measure the unintegrated nonlinear candidate, audio-device scheduling,
host overhead, processing-thread allocation or actual callback misses.
Reported overruns compare steady-clock durations to *simulated* deadlines.

An initial **0.05 s, 48 kHz, eight-voice diagnostic** on that machine
gives the following rounded median microseconds per complete legacy
processor callback (deadline: 20.83 / 333.33 / 5354.17 µs for 1 / 16 /
257 samples). `over` counts callbacks longer than the simulated deadline;
none of these runs is a 30-second tail-latency qualification:

| Factor | 1 sample median / over (2400) | 16 samples median / over (150) | 257 samples median / over (10) | Reported latency (samples) |
| --- | --- | --- | --- | --- |
| 1x | 6.38 / 0 | 66.17 / 0 | 1030.13 / 0 | 0 |
| 2x IIR | 9.00 / 3 | 123.42 / 0 | 1967.54 / 0 | 4 |
| 4x FIR | 16.67 / 195 | 237.54 / 0 | 3820.29 / 0 | 61 |
| 8x FIR | 30.63 / 2400 | 460.29 / 150 | 7489.46 / 10 | 65 |
| 16x FIR | 57.67 / 2400 | 919.00 / 150 | 14501.54 / 10 | 67 |

One longer **30-second legacy 1x** baseline at 48 kHz, 257 samples and
eight voices produced 5,604 offline callbacks: median `1044.50 µs`,
nearest-rank 99.9th percentile `2681.04 µs`, maximum `4746.67 µs`,
zero callbacks exceeding the simulated `5354.17 µs` deadline, and zero
reported processor latency. This single configuration does not establish
the remaining rate/block/voice combinations, allocation safety, real
audio-device callback behavior or a candidate cost budget.
At the same settings with **16 active voices**, a separate 30-second
legacy 1x run produced 5,604 callbacks: median `2140.96 µs`, 99.9th
percentile `2478.50 µs`, maximum `4625.38 µs` and zero simulated
deadline exceedances. These runs were offline and do not measure an
audio-device callback or the nonlinear candidate.

A separate **0.02-second 16-voice spot check** at 48 kHz finds medians
near `10.08/17.33/32.17/60.33/117.92 µs` at one-sample blocks and
`2.11/4.13/8.01/15.22/29.73 ms` at 257-sample blocks for
1x/2x/4x/8x/16x. The 257-sample results contain only four callbacks
per factor, so their tails are not meaningful. At 257 samples the 4x,
8x and 16x cases exceed the simulated deadline on all four calls;
the 2x case exceeded on one. The baseline includes no nonlinear ladder.

These measurements alone do not establish a repeatable audible 8x-to-16x
benefit. Thomas subsequently retained 16x for the candidate only as an
offline-render option; the short cost probes do not justify real-time 16x.
Interim legacy still allows 16x in playback. Retaining candidate 8x or
higher polyphony does **not** establish safe real-time operation on the M1 Pro.
The remaining five-host-rate,
1/16/257-block, 8/16-voice, 30-second legacy baseline and the
**complete candidate** processor Release matrix still need to be measured.
Per-path support and limitations require approval before production
integration. The earlier hypothetical M5 Pro scaling is not substitute
evidence for this newly selected physical target.

In this first 48-case, 48/96 kHz, block 1/16/257, 8/16 filters,
2x IIR and 4x/8x/16x FIR run, at 48 kHz, 257-sample blocks, 8
filters, the simulated deadline is `5354 us`: median costs were about
`1510`, `3037`, `6046`, and `11517 us` respectively. The 8x path
exceeded the simulated deadline on 93 of 94 blocks and 16x on all
94. At 96 kHz with 257-sample blocks and 8 filters, the deadline is
`2677 us`: 4x/8x/16x median costs were approximately `3017`, `5775`
and `9125 us`, exceeding that deadline on 186/187, 187/187 and
187/187 calls. For 48 kHz, 16-sample blocks and 16 filters the 4x
median was `380 us` against a `333 us` deadline, exceeding it on
1222/1500 blocks; higher factors were still slower. All 48 probes
recorded zero solver fallback/non-finite samples. These **short,
isolated** measurements indicate that simply replacing the 2x
quality path with 8x/16x is not a viable CPU-gate pass on this machine.
Four additional Off-path baselines at 257 samples and 8/16 filters
give approximately `779/1516 us` median at 48 kHz and `764/1524 us`
at 96 kHz. On these short runs, even Off has occasional simulated
deadline exceedances, so neither zero-overrun nor precise tail-cost
claims can be inferred from this tool. These isolated measurements were
originally collected on a diagnostic machine subsequently selected as
the physical M1 Pro CPU measurement target; they are still not complete-
processor evidence.
An earlier proposed CPU target was a hypothetical Apple M5 Pro assumed
to offer twice the M1 Pro single-core speed and ten usable cores. It is
**not available for measurement**, so no hardware, toolchain or scheduling
results have been established on that hypothetical machine. Dividing times
by 2 or 10 cannot establish callback tails or a production CPU gate.
Reducing voice count, optimizing the solver or redefining supported
quality/rate/block policies requires explicit product review and a new
complete-processor Release benchmark; do not silently waive the CPU or
spectral criteria.

An **experimental parallel filter-only** mode accepts an optional
`lanes` argument (1–10) on `VektLadderCost`. It preallocates lane buffers
and starts persistent worker threads outside the timed loop; independent
candidate ladders run on worker lanes before the audio thread mixes
their results. It does **not** modify Mono or make the production audio
thread thread-safe. Atomics with blocking `wait`, OS scheduling and
synchronization on every callback are inappropriate as an unreviewed
real-time implementation. The optional untimed serial rerender compares
every output sample against the parallel render; the probe fails if the
maximum absolute difference exceeds `2e-6`.

An initial 36-case, half-second diagnostic matrix (48/96 kHz,
1/16/257-sample blocks, 8/16 ladders, 8x/16x FIR, four lanes and ten
lanes where 16 ladders are available) records zero solver fallback.
At 48 kHz, 257-sample blocks, eight 8x FIR ladders, four lanes gave
about `1733 us` median and `1915 us` 99.9th percentile versus a
`5354 us` simulated deadline. At 96 kHz, 257 samples, eight 8x FIR
ladders, four lanes gave about `1655 us` median and `1886 us` 99.9th
percentile versus `2677 us`. However, at 96 kHz, 16 samples, eight
8x FIR ladders, four lanes gave about `145 us` median and `249 us`
99.9th percentile against a `167 us` deadline (94/3000 calls exceeded
it); at 96 kHz, *one-sample* callbacks, eight ladders and four lanes,
the observed 99.9th percentile was about `71 us` against a `10.4 us`
deadline. Ten lanes are not automatically better: with 16 ladders at
48 kHz, 16 samples and 8x FIR, the first run's 99.9th percentile was
about `275 us`, but a subsequent same-configuration run reported about
`1495 us`. Even on the diagnostic M1 Pro, dispatch tails are not
repeatably within the proposed 75%-of-deadline policy. Synthetic
2x-single-core or ten-core extrapolations to an untested M5 Pro cannot
replace 30-second full-processor measurements and a named listening
review. Parallelization is **research**, not production integration.

The complete `build/dev/tests/vekt_dsp_tests --reporter compact` executable
was rerun on 26 September 2026 after the spectral/reference test additions:
**6,595,519 assertions in 235 cases passed**. This validates the current
development test executable, not the unimplemented production candidate,
unapproved numerical gates, or the separate Release timing tool.

The 48 kHz coherent 300/500 Hz, 0.25-peak-per-tone probe at 1 kHz cutoff,
resonance 0.85 and +12 dB drive also passes a candidate-versus-32-substep
offline-reference regression. The reference's 16/32-substep component comparison
converges (less than `0.001 dB` per fundamental and `1e-5` peak per third-order
product), with zero unconverged steps. Candidate/reference fundamental error is
below `0.03 dB` each and third-order absolute peak error below `1e-3` each;
the largest observed third-order error is approximately `2.05e-4`. This is one
raw-tap operating point, **not** a supported-rate or quality-path IMD limit.

For a separate low-level (`0.001` peak), zero-resonance/zero-drive stopband
probe at 500 Hz cutoff, 4/8/12 kHz probe frequencies and all five planned host
rates, each analytical output peak is below `1e-6`. The largest absolute
output-peak error is about `2.41e-8`, within the proposed `1e-7` deep-stopband
review target. Relative gain error spans approximately 0.28 to 0.95 dB;
relative dB and phase are diagnostics rather than acceptance measures here.
This does not establish the other branch of the stopband policy, where the
analytical output peak is at least `1e-6`, or a full cutoff/quality matrix.

An overload audit found that the offline reference had clipped its feedback and
stage Newton iterates to `[-24, 24]`, although the equation's roots can lie outside
that range. For example, a 512-host-sample alternating `+4/-4` input at +24 dB
returned hundreds of unconverged steps at low/mid cutoff and every step
unconverged at the cutoff ceiling. The development reference now brackets both
roots without clipping the iterates, retaining the `[-24, 24]` driven-input and
updated-state bounds. A regression gate covers all five rates, floor/1 kHz/ceiling,
resonance 0.5/1 and 16 reference substeps (245,760 internal steps); these cases
converge with residual at most `1e-13`. The reference must preserve the host
prewarped pole: `gHost = tan(pi cutoff / hostRate)` and `gSub = gHost / N`.
An earlier `tan(gHost / N)` implementation incorrectly re-warped the pole and
crossed a tangent branch at the cutoff ceiling. A one-substep regression against
the candidate now guards this identity. Increasing substep factors must still
establish *render* convergence before high-cutoff spectral comparisons.

An additional 48 kHz, `0.45 * hostRate` cutoff, resonance 0.85, +12 dB drive,
peak-4 sine at 0.07 times the host rate exposes the difference between *solver*
convergence and *reference-render* convergence. Over 2,048 host samples (last
1,024 compared), the corrected 16-to-32-substep output RMS difference is about
`0.01109` (peak `0.03650`); 32-to-64 is about `0.002689` RMS (peak `0.009215`).
All tested factors report zero unconverged steps. The decreasing difference
does not qualify 64 substeps as ground truth. A further isolated `-O2` probe
at 128, 256 and 512 substeps measures successive RMS differences of about
`0.000672`, `0.000168` and `0.000042`, respectively, on the same last-half
window. The approximately fourfold decrease is consistent with converging
substeps but even 256-to-512 is not zero; high-cutoff overloaded alias/IMD
reference comparisons still require an explicit error budget and more cases.

### Provisional Numerical Limits

The currently demonstrated limits are provisional and do not accept ADR 0005:

- low-level analytical gain error below 0.01 dB and phase error below 0.001 radians
  outside the deep stopband;
- a representative 48 kHz deep-stopband gain diagnostic below 1 dB; this is **not** a
  supported-rate limit: the 24 kHz report exceeds 1 dB at 8 and 12 kHz while absolute
  output-peak error remains below `5e-10`. The stopband tolerance policy remains open;
- candidate/reference fundamental error below 0.03 dB, second-component error below
  `1e-5` and third-component error below `8e-4` for the existing raw-output level
  probes, including the 24 kHz test fixture;
- candidate solver residual no greater than `2e-7` and offline-reference residual no
  greater than `1e-13` for the previously passing representative probes; and
- zero unconverged and zero non-finite samples or steps.

IMD, aliasing, modulation-artifact, self-oscillation and Release-cost limits remain to
be derived from the completed matrix and documented listening checks. They must be frozen
before ADR 0005 can become Accepted.

At all five planned host rates, a 1 kHz cutoff, 1.0-peak impulse and 2-second
development-only ringdown decays at resonance 0.98 and sustains at resonance 1.0.
In the last 0.5 seconds, resonance 0.98 peaks below `9e-7`, while resonance 1.0
has 500 positive-going crossings, roughly 1 kHz crossing frequency, peak amplitudes
`0.00375..0.00701` and RMS `0.00261..0.00465`. These are observed values, not
accepted self-oscillation limits; near-threshold behavior, startup from silence,
other cutoff/drive settings, quality paths and spectral purity remain open.

An additional development-only 2-second, 1.0-peak impulse at the **10 Hz floor**
and `0.45 * hostRate` ceiling, with zero drive and five host rates, measures the
last 0.5 seconds separately. At 10 Hz, resonance 0.98 tails peak from
`2.80e-5` to `1.19e-4` (above the proposed 1 kHz-only `1e-5` decay target);
at resonance 1, peaks range from `4.49e-5` to `1.92e-4`, with RMS from
`3.17e-5` to `1.36e-4` (below the 1 kHz-only `1e-4` minima at some rates).
Both have five positive crossings in the measured half-second, so the 10 Hz
frequency estimate has limited resolution. At the cutoff ceiling, resonance
0.98 tails peak below `9.5e-8`; resonance 1 peaks from `0.00233` to `0.00502`,
and its positive-crossing estimate lies within 1% of the ceiling cutoff.
All these development-only boundary runs had zero non-finite or unconverged
samples. The floor result **does not pass** the 1 kHz amplitude policy; choose
floor-specific duration, decay, onset and purity limits from controlled data
before any self-oscillation gate can be accepted. Quality paths remain untested.

To resolve the short-window mismatch, a development-only 30-second **raw candidate
tap** impulse probe at 10 Hz cutoff, zero drive, Off (no up/downsampler), 1.0
input peak and resonance 0.98/1.0 was run at all five host rates. With 5–10 s
and 25–30 s five-second windows, resonance 0.98 falls from early peaks
`1.03e-5..4.00e-5` to late peaks `1.40e-6..1.46e-6` (late/early peak ratio
at most `0.137`), with late RMS `9.63e-7..1.04e-6`. Resonance 1.0 has
late peak `5.67e-5..1.92e-4`, late RMS `3.94e-5..1.36e-4`, and late/early
RMS ratio at least `0.999`; its 25–30 s window contains 50–51 positive-going
crossings and estimates `10.000..10.018 Hz`. All runs report zero solver
fallback/non-finite samples and residual at most `2e-7`. The output first
crosses `1e-5` between 7.0 and 13.1 ms **after the impulse**: this measures
impulse response, not spontaneous self-oscillation startup from silence.

**Proposed floor-specific review targets, not product-approved acceptance:**
with this exact 30-second, 10 Hz, Off-path, zero-drive impulse protocol and
5–10/25–30 s windows, require an onset crossing of `1e-5` within 20 ms;
at resonance 0.98, late peak below `2e-6`, late RMS below `1.5e-6`, and
late/early peak ratio below `0.2`; at resonance 1.0, late peak above
`3e-5`, late RMS above `2e-5`, late/early RMS ratio above `0.8`, and
25–30 s positive-crossing frequency within 0.5% of 10 Hz using at least
49 crossings. Require zero solver fallback/non-finite samples and residual
at most `2e-7`. The development regression checks these review targets at
five rates, but **does not complete the gate**: the absolute floor tail may
reflect the solver tolerance, these thresholds have not been listening-reviewed,
and other rates through the quality paths, different impulses/drives, full
spectral purity, and noise-seeded startup remain unmeasured. The 1 kHz
two-second limits above remain specific to 1 kHz; neither policy is silently
substituted for the other.

A separate 48 kHz development probe now runs that same 30-second impulse
through the prepared 2x/4x IIR/FIR upsample/ladder/downsample paths at
resonance 0.98 and 1.0 (128-sample blocks). At resonance 0.98, late peaks
are `1.42e-6..1.50e-6`, RMS `9.78e-7..1.06e-6`, and 10 Hz coherent
amplitudes `7.42e-7..8.92e-7`; the late crossing estimates are
`9.886..9.895 Hz`, **not** within 0.5% of 10 Hz. Decaying, low-amplitude
tails are not suitable for the resonance-1 frequency criterion. At
resonance 1.0, late peaks are `1.88e-4..2.09e-4`, RMS
`1.33e-4..1.48e-4`, and crossing estimates `10.001..10.003 Hz`.
The measured 20 Hz and 30 Hz coherent amplitudes are respectively
`3.20e-8..8.72e-8` and `1.75e-8..4.88e-8` at resonance 1.0;
these selected harmonics are **not** a full-spectrum purity or noise-floor
measurement. Each path had zero fallback/non-finite samples and residual
at most `2e-7`. A second, reset one-second exact-zero-input render stays
exactly zero in all paths; this rules out spontaneous startup **from exact
digital silence only**, not startup from noise or perturbations. No quality
path amplitude or harmonic thresholds are approved by this probe.

A separate deterministic **noise-seeded** 1 kHz startup probe at 48 kHz
uses a fixed-seed LCG with samples in `[-1e-6, 1e-6)`, zero drive,
resonance 0.98/1.0, 128-sample blocks, and Off/2x/4x IIR/FIR paths.
Over two seconds, comparing the first and last 0.5 seconds, the
resonance-0.98 late RMS is `2.39e-7..3.34e-7`, while resonance-1.0
late RMS is `3.06e-6..4.80e-6` (early RMS `9.02e-7..1.38e-6`).
No path had a non-finite sample or solver fallback, and residual remained
at most `2e-7`. This establishes measurable growth from this *specific
continuous seeded perturbation*, not a defined time to audible onset,
free-running startup after noise removal, or the behavior at the 10 Hz
floor and cutoff ceiling. It does not replace the zero-input test or
documented listening checks. Startup limits remain unapproved.

An isolated Release-optimized
500,000-sample benchmark comparing the staged float solver with the development double
solver measured approximately 319 vs 362 ns/sample on one nominal sine, and 143 vs
167 ns/sample on one overloaded low-cutoff sine, respectively. These preliminary
single-run timings are not a production-path CPU budget or an accepted quality cost.

### Proposed Acceptance Package — Requires Product Approval

The following is a **proposal for review**, not an implemented or passing gate.
ADR 0005 stays Proposed and the report's `production_integration_allowed` stays false.
The revised 26 September 2026 product scope permits 1x as Mono's real-time
default when 2x cannot meet an actual measured CPU budget. The candidate
plans separate Playback Quality (1x/2x/4x/8x, default 1x) and Offline Render
Quality (follows playback by default; an explicit override offers
1x/2x/4x/8x/16x). 16x must never run in playback.
The unchanged legacy GUI has one five-choice setting without offline gating.
This does **not** integrate the nonlinear candidate or exempt retained paths
from spectral, latency, stability and applicable CPU/offline safety review.
Design and test the offline eligibility and mode-transition policy before
candidate handoff; do not silently substitute quality or reinterpret states.
Thomas approved using Playback Quality whenever offline status is unverified
or explicitly real-time, and Offline Render Quality only when the host
explicitly reports offline processing. Switch only at a safe boundary and
report the effective quality. The exact boundary, latency transition and
host-specific guard are still pending; no guard exists in legacy Mono.
Thomas approved following Playback Quality (initially 1x) for new sessions
until the user sets an independently recalled Offline Render Quality override;
clearing the override resumes following subsequent playback-quality changes.
Thomas selected `prepareToPlay`/reinitialization as the only boundary for
changing effective quality, never during active processing. For the exceptional
case where offline status disappears without reinitialization while 16x is
active, the approved last-resort candidate guard is to check status per
callback, emit silence rather than process 16x, latch an off-thread
diagnostic and require reinitialization. Do not change DSP state or latency
in the callback. Thomas also approved offering offline 16x only on hosts
whose export and reinitialization lifecycle is verified. On other hosts,
cap available offline overrides at 8x, preserve any stored 16x preference
and report its unavailability and the effective quality. Thomas approved
blocking offline export with a visible explanation if such a preference is
recalled; never silently render at a lower quality. JUCE's normal plugin
`processBlock` has no portable host-export cancellation result: demonstrate
that an eligible host can prevent an invalid file before claiming this gate
passes. Silence plus a warning is not a blocked export. If no enforceable
host path exists, do not offer 16x there and seek a product revision rather
than implying cancellation. The exceptional interruption, host eligibility,
saved-setting and latency behavior still need implementation and tests.
Interim legacy is unchanged and not guarded.
Thomas selected Ableton Live 12 Suite with VST3 as the first unqualified host
to investigate. Live 12.4.6 and an arm64 Vekt Mono VST3 are available locally,
but no Live lifecycle or export-blocking test has passed. Live can export in
real time when routed to external hardware. VST3 requires `setupProcessing`
for an offline/realtime mode transition; measure both that interface and
JUCE's preparation/mode reporting before treating any Live export as eligible.
In the vendored JUCE VST3 wrapper, `setupProcessing` updates the offline flag
but explicitly does not call `prepareToPlay`; `setActive(true)` does. A
conforming VST3 offline transition therefore does not by itself meet the
approved prepare-only quality-switch boundary. Check whether Live actually
deactivates/reactivates before export and negotiates the resulting latency;
no such host observation or export cancellation has been verified.
Old normalized host automation/state values are not migrated: the former
`1.0` "High" value now denotes 16x, not 2x. Mono's pre-alpha compatibility
policy permits this break, but existing sessions should be recreated or
their quality choice checked before use.

**Current decision handoff (26 September 2026):** The development solver,
selected self-oscillation/IMD probes and focused tests pass their stated
checks. The full development test executable against the binary containing
the final three-stimulus 8x/16x regression passed (6,627,140 assertions,
243 cases; exit 0). The focused three-stimulus case passed separately
(30,980 assertions); the earlier focused spectral selection passed
(653 assertions, 8 cases). These passes do not meet the product gates.
The spectral gate is **not met**: a +12 dB, resonance-0.98, 48 kHz
7 kHz input yields a 5 kHz non-harmonic component near `-48.6 dBc`
through both 2x paths, versus about `-78.86 dBc` with four offline
substeps at the same 2x host-output tap. Fixed-frequency scans show
improved maxima at 44.1/48/88.2 kHz but a slightly higher 47 kHz fold
at 96 kHz after substepping. Neither a four-step offline reference nor
the internal-tap attribution constitutes a real-time mitigation or a
converged full-spectrum reference. Before approving a 2x quality setting
or its proposed `-60 dBc` alias ceiling, validate a feasible mitigation
over the operating matrix or explicitly revise the product limit after
documented listening checks and product review. The user permits *investigating*
a replacement quality path, but the measured 8x/16x FIR spectral
improvement comes with simulated deadline exceedances in the sequential,
filter-only Release cost probe on Apple M1 Pro. **This is diagnostic evidence only**:
the proposed Apple M5 Pro CPU target and its 2x single-core / ten-core
speed assumptions are hypothetical and unmeasured. Existing Mono renders
voices sequentially per sample; no replacement has demonstrated the
proposed spectral and CPU expectations together on a product CPU target.
The other open gates are high-cutoff reference
convergence and broad-spectrum/IMD/modulation coverage; floor/ceiling
and noise-seeded onset limits; the *candidate* in a complete production
render-path Release CPU/allocation test; and documented unblinded checks with a
named reviewer's explicit numerical/CPU/listening sign-off. The existing
Release prototype report and legacy Mono renderer cannot substitute for
that candidate production-path test. No approval or integration is
implied by the passing development tests.

### Feasibility decision before matrix expansion (proposed workflow)

The maintained status, next action, research comparison and stop/go checkpoints
are in `docs/MONO_LADDER_ACCEPTANCE_PLAN.md`; update that file with each
new result or decision. This document retains the detailed measurements.

An initial **development-only**, bounded two-/four-substep solver variant is
implemented in `plugins/vekt_mono/Source/NonlinearTptLadder.cpp` alongside the
unchanged one-step path. It linearly interpolates incoming sample and
cutoff/resonance/drive endpoints, divides the incoming-rate prewarped TPT
coefficient by the substep count, and uses the candidate's existing bounded
solve at each substep. A focused independent-reference comparison at 48/96 kHz
passes at 4,096 samples per rate within `2e-5` absolute output error,
with zero fallback/non-finite steps. A separate 512-host-sample 2x IIR
modulated probe passes bit-exact 1- versus 127-sample-block output and
freshly prepared rerenders. In a settled 48 kHz, 7 kHz-tone,
resonance-0.98 2x IIR coherent scan, the largest non-direct-harmonic
host bin is 5 kHz except for the two-step +24 dB case (3 kHz):
one/two/four-step largest-bin levels are approximately
`-48.59/-73.76/-78.86 dBc` at +12 dB and
`-24.27/-42.88/-59.28 dBc` at +24 dB drive. The two-step +24 dB maximum
is at 3 kHz, not 5 kHz: a one-bin comparison would miss this limitation.
These are stimulus-specific diagnostics, **not** a full-spectrum or
reference-aligned spectral pass;
neither variant has a complete-processor Release CPU measurement
or is selected for production. Fixed-frequency rate coverage,
modulation spectra, high-cutoff behavior and the rest of this workflow
remain open.

An optional serial `steps(1|2|4)` argument on `VektLadderCost` compares the
same filter-only workload (one 2x IIR bank, eight separate ladders, 330 Hz
input, 1 kHz cutoff, 0.85 resonance, +12 dB drive) in the Release build
on the physical M1 Pro. In half-second offline runs with 257-sample blocks,
the one/four-step medians are about `1554/6219 µs` at 48 kHz (simulated
deadline `5354 µs`, four-step exceedances 94/94), and `1557/6001 µs` at
96 kHz (deadline `2677 µs`, four-step exceedances 187/187). A separate
two-step half-second diagnostic at the same settings measured about
`3136 µs` median at 48 kHz (0/94 simulated exceedances) and `3118 µs`
at 96 kHz (187/187 simulated exceedances). There are zero
reported solver fallbacks or non-finite samples. These short serial,
filter-only timings exclude complete Mono voice/processor cost and actual
audio-device scheduling. Nonetheless, adding processor work cannot make
these unoptimized sequential substep, eight-ladder configurations fit
the measured M1 Pro 96 kHz simulated deadline; four steps also miss at
48 kHz. Do **not** propose either as a general 2x real-time replacement
under those operating conditions. This does not
rule out a different mitigation or a reviewer-approved narrower policy.

The 243-case, 6,627,140-assertion full-suite result above predates the
substep variant. The full development suite with the four-step variant
passed 246 cases and 6,645,095 assertions (exit 0). The subsequently
added two-step comparison is not included in that full-suite result;
the focused solver/reference/feasibility selection with two steps passes
266,499 assertions in 14 cases. Neither result establishes the spectral
or complete-processor CPU gates.

**1x-first default feasibility (diagnostic, not approval):** At 48 kHz,
10 kHz cutoff, 7 kHz/0.5-peak input, resonance 0.5/0.98 and +12/+24 dB
drive, a settled 0.1 s coherent 1–23 kHz host-bin scan finds the one-step
1x candidate's strongest non-direct-harmonic bin at 1 kHz, about
`-28.01/-25.57 dBc` at +12 dB and `-11.31/-11.51 dBc` at +24 dB (in
resonance order). At that same 1 kHz bin, the independent 16/32-step
reference returns are about `-51.4/-51.5`, `-50.4/-50.5`,
`-37.85/-37.87` and `-38.46/-38.48 dBc`, respectively. The 2-step
candidate reduces the largest bin to `-46.35/-43.31 dBc` at +12 dB;
at +24 dB its largest moves to 5 kHz (`-26.57/-25.33 dBc`), roughly
33 dB above the corresponding 32-step reference-return bin. The 4-step
candidate's maxima are about `-48 dBc` at +12 dB (13 kHz) and `-38 dBc`
at +24 dB (1 kHz). These reference values are **one sample returned per
host interval without a reconstruction low-pass**; close 16/32 bin
agreement does not make them a band-limited alias ground truth. These
measurements cover one coherent tone, not modulation, independent rates,
continuous spectra or perceived quality. Do not approve or reject the
1x default by equating a non-direct-harmonic bin with an audible alias.
The focused 1x diagnostic passes 120 assertions in one case on the rebuilt
development test binary; this is a measurement validity check, not a
spectral acceptance test. A combined focused run was interrupted before
completion, and the prior 246-case full-suite pass predates this 1x test.

In separate half-second serial **filter-only** Release runs on the M1 Pro
at 257-sample blocks, the 1x two-step variant with eight ladders measured
about `1583 µs` median at 48 kHz and `1579 µs` at 96 kHz (0 simulated
deadline exceedances out of 94 and 187 callbacks, respectively). With
16 ladders, medians were about `3183/3091 µs`: 2/94 simulated exceedances
at 48 kHz and 187/187 at 96 kHz. A four-step eight-ladder run exceeded
187/187 at 96 kHz. These are not synth voices, complete-processor
measurements, repeatable tail qualifications, or actual callback misses.
The unoptimized sequential two-step, 16-ladder 96 kHz configuration cannot
meet that simulated deadline by adding processor work. The 1x one-step
candidate has no accepted sound-quality gate; the 1x two-step variant has
no general CPU or spectral pass. Pause candidate processor integration
pending band-limited 1x spectral attribution and named listening checks.

**Filtered 1x reference-method probe (26 September 2026):** The independent
reference can now expose raw internal substep samples without changing its
existing host return. A non-causal, symmetric Blackman-windowed sinc FIR
low-passes 16x/32x raw outputs before decimation, with 32 host samples of
context on either side. The focused test checks 1 kHz-spaced passband
points from 1–15 kHz
(largest gain error about `4.19e-5`) and stopband points from 25–47 kHz
(largest gain about `2.49e-5`), an injected 47 kHz tone
folding below `1e-4` peak at 1 kHz, and bit-exact identity between each
reference host return and the captured last substep. At 48 kHz, a 7 kHz,
0.5-peak tone, 10 kHz cutoff, resonance 0.98 and +12 dB drive gives
one-step/filtered-16x/filtered-32x levels of about
`-25.57/-49.82/-49.89 dBc` at 1 kHz and
`-45.43/-87.24/-87.59 dBc` at 5 kHz. At +24 dB the corresponding
levels are `-11.51/-38.39/-38.41 dBc` and
`-27.47/-58.67/-58.83 dBc`. The two filtered references differ by
at most `0.36 dB` **at these two bins only**; the one-step candidate
exceeds the filtered-32x level by more than 20 dB at each measured bin.
The focused test passes **38,578 assertions in one case**. This is
evidence of excess components under this single stimulus, not a
full-band alias limit or an audible-failure decision. Filter transition-band
and between-grid response, input/reconstruction error, higher-factor
full-render convergence, other rates, modulation and listening remain open.
The previous full-suite result predates this reference-method test.
Thomas approved the broader 1x artifact **review method** in
`docs/MONO_LADDER_ACCEPTANCE_PLAN.md` on 26 September 2026: attribute
reference-aligned excess components across rates and controls, followed by
documented unblinded listening. The listening method was revised on
27 September 2026; this did not approve a 1x numerical ceiling, this
candidate's sound or the limited reference probe as an acceptance result.

This changes the order of work, **not** revised ADR 0001 or the acceptance
The planned candidate retains 1x/2x/4x/8x, with 1x default.
Every retained candidate path needs spectral, latency
and stability review; real-time paths need complete-processor CPU review. The physical
Apple M1 Pro is the measurement target. Selectability does not promise glitch-free operation
at every rate, block size and voice count, but a named product reviewer must
approve operating conditions and exceptions. Hypothetical M5 Pro scaling and
parallel filter-only timing are not production CPU evidence.

**Replacement direction approved 27 September 2026:** Qualify coupled as the
preferred solver for replacing legacy across the retained paths; nested remains
a development comparison, not a release fallback. The existing measurements
do not approve a host-path switch. Keep legacy production until coupled passes
sound, safety, the unchanged M1 Pro 1x baseline timing target and approved
higher-mode/host gates. After acceptance, validate a coupled production build
and only then remove legacy DSP in a separately tested cleanup. This policy
does not reclassify earlier nested or legacy results as coupled evidence.

**Development listening access (27 September 2026):** The separately built
standalone Ladder Preview selects coupled at 1x, and `audio-lab-coupled`
configures Audio Lab to select the coupled Mono processor at 1x. The ordinary
Audio Lab preset and the normal Mono plugin still select legacy. Coupled preview
now exercises 1x/2x/4x/8x; 16x is no longer selectable. The development
editor explicitly identifies the effective engine (`DEV COUPLED Nx`,
`DEV NESTED 1x`, or `DEV PREVIEW: LEGACY`). Use
`scripts/run-mono-coupled-audio-lab.sh` for the coupled Audio Lab build and
verify the label when selecting Mono; start at a safe listening level because
earlier legacy/candidate renders had different peaks. Thomas reports a manual
coupled 1x audition sounds much better than legacy and favors removing legacy.
The report does not specify monitoring conditions or separate resonance,
overload, modulation and gain observations. This preference does not verify
those behaviors or retained higher-quality modes. Focused
processor/UI and coupled tests pass 375,998 assertions in nine cases.

**Listening report (27 September 2026, Thomas, product owner):** "I tested the
coupled 1x mode. Sounds much better than legacy. I think legacy can be removed."
This is a firsthand, unblinded subjective preference from the development
audition, not a level-matched or randomized comparison. No monitoring conditions,
test material or per-control notes were supplied; do not infer a
headroom, aliasing or higher-quality-path pass. Coupled remains development-only
at 1x; the normal host plugin and preview 16x still run legacy. Development
coupled paths at 2x/4x/8x have not passed their per-path gates. Continue
with documented unblinded checks on representative patches/settings and complete-path safety,
timing, retained-quality and host validation before production cutover; remove
legacy only after a validated replacement exists for every retained path.

**Thread-CPU callback diagnostic (27 September 2026):** The development Release
cost tool accepts an opt-in `candidate-coupled cpu` mode, sampling the calling
thread's CPU clock around callbacks. A 30-second 44.1 kHz/128-sample, eight-
voice 1x run recorded five wall-time overruns among 10,336 callbacks; all five
used under `2902.494 us` of thread CPU (maximum overrun CPU `1916.750 us`,
maximum wall minus CPU `8483.334 us`). Its p99.9 was `1175.458 us`, maximum
`9154.625 us` and measured wall-time verdict **fail**. A separate 2-second
48 kHz/257-sample run found three wall overruns, all with thread CPU below its
deadline. Runs were offline under the default scheduler; the 30-second run
overlapped the short diagnostic, and other system activity was not controlled.
This is consistent with time off-CPU, not proof of scheduler causation or
actual device misses. CPU-clock sampling adds overhead and does not alter the
approved wall-time rule. The tool's exit 0 and zero intercepted callback C++
`new` do not establish the full safety gate. The 30-second log at
`/tmp/vekt-coupled-thread-cpu-30s-20260927.log` is temporary evidence only.

A further *sequential*, not controlled-scheduling, pair on MacBookPro18,1
processed 30 seconds of offline audio per case using `caffeinate -i`, the
default scheduler and CPU sampling. At 44.1 kHz/128 samples, eight voices and
1x coupled: 10,336 callbacks, p99.9 `2649.959 us` versus a 75%-deadline
target of `2176.871 us`, maximum `16797.625 us` and eight wall-time
exceedances; all eight had CPU time below the `2902.494 us` deadline (maximum
overrun CPU `1929.459 us`, maximum wall minus CPU `15261.041 us`). The
following 48 kHz/257 run: 5,604 callbacks, p99.9 `3177.416 us` versus
`4015.625 us`, maximum `4754.000 us`, zero simulated exceedances, measured
timing rule met **in this run only**. Process snapshots showed varying CPU
activity from WindowServer and VS Code helpers. `caffeinate -i` prevents idle
sleep, not contention. Exit codes were zero with zero covered callback C++
`new` and zero reported coupled unconverged/non-finite samples. These are
offline CPU-clock diagnostics, not actual callback misses or evidence that the
full gate passed; the root cause remains unknown. The temporary log is
`/tmp/vekt-coupled-sequential-cpu-valid-20260927.log`.

1. **Freeze the decision contract, not arbitrary test counts:** Name the
   product reviewer and approve the supported rate/block/voice envelope per
   quality, representative cases and boundary combinations, the audible and
   numerical alias/IMD/modulation and overload limits (including Off),
   stopband tolerance, resonance/startup and solver fallback policies,
   allowed latency, CPU/allocation policy and hardware. Thomas approved the
   30-second complete-candidate Release 1x CPU *target* for 44.1/48 kHz,
   eight voices and 128/257-sample blocks: zero processing-thread allocations
   and solver fallbacks, p99.9 under 75% of the simulated block deadline and
   zero simulated exceedances; report maximum and latency separately. This is
   not a measured pass. The `-60/-80 dBc` figures remain proposals. Retaining
   a mode requires explicit per-path evidence; changing a GUI choice requires an
   approved ADR 0001 revision.
2. **1x sound first:** Compare the one-step default and at most two specified
   mitigations with a progressively converged, adequately band-limited
   higher-rate raw-tap reference, accounting for latency, direct harmonics
   and expected modulation sidebands. Cover selected absolute frequencies,
   rates (including 44.1/48/96 kHz), resonance, drive/overload and modulation;
   report worst host-band frequencies and levels, not just 1 kHz. Arrange
   documented unblinded listening on representative patches and settings with
   the **named** reviewer. Blinding remains optional if a decision is uncertain.
   Coherent-bin scans and unfiltered reference returns alone cannot pass
   an alias or audibility gate. If no 1x sound candidate merits further
   work, stop and revisit the model or product target, not 2x CPU.
3. **Measure the complete candidate while developing its sound:** An explicit
   development-only 1x processor selection now exercises Mono's oscillator,
   envelope, voice mixing and candidate filter without enabling it in the
   host plugin. Check gain/control mapping, reset/state, correct latency,
   allocation and bounded work. Compare Release candidate
   and legacy on the physical M1 Pro, first at the approved 48 kHz/257/8
   pilot, then at every approved 1x combination (44.1/48 kHz, 128/257
   samples, eight voices). Run 1/16-sample, 96 kHz, 16-voice and other
   exposed combinations as diagnostics until their limits are decided;
   include static/modulated driven cases and 30-second tail measurements.
   Record build ID, duration, allocations, median, p99.9, maximum and
   *simulated* deadline exceedances.
   The default `VektMonoProcessorCost` reports `engine=legacy`; its explicit
   development-build `candidate` mode times the complete 1x processor.
   `VektLadderCost` is filter-only. No short timing probe passes the CPU gate
   or measures actual callback misses.
4. **Separate default feasibility from release scope:** If 1x passes sound
   and complete-path CPU review, record a *credible default-path candidate*,
   not ADR acceptance. Qualify every retained higher mode for approved
   spectral, stability, latency and complete-path cost conditions. Qualify
   16x separately for verified offline rendering, including how playback,
   export, state recall, latency and transitions behave; do not infer an
   offline guard from the current legacy implementation. A 1x-only
   or mixed legacy/candidate release requires explicit ADR 0001/0005
   revisions and quality-switch validation, not a silent waiver.
5. **Close the model and obtain sign-off:** Finish agreed analytical and
   progressively converged nonlinear-reference coverage, IMD, modulation,
   self-oscillation, stopband, fallback and long-run safety checks. Record
   documented listening checks, approved limits, measured exceptions and the
   named reviewer's product decision in ADR 0005. Only then change its
   status and consider production integration. If the approved target is
   infeasible, record rejection or supersession instead of indefinite
   Proposed status. Keep legacy production and
   `production_integration_allowed=false` until acceptance.

Record the sample-rate, active internal rate, quality/filter type, input and drive
levels, cutoff/resonance trajectory, block size, measurement window, tool version,
machine and Release build identifier for every result. Exercise the entire planned
matrix above, with longer 2- and 30-second renders at the boundaries. Compare the
same *raw* output tap; measure optional compensation separately. Validate the
offline reference's pole-preserving coefficient and convergence at successively
higher substep counts before using it as a spectral ground truth.

Proposed numerical *review targets* (not inferred from the representative report):

- Solver: zero unconverged/non-finite samples, no processing-thread allocation,
  bit-exact rerenders and block-size invariance, and maximum residual `2e-7`
  throughout all supported host/internal rates and controls. A single fallback
  incidence fails this policy and requires an explicitly approved continuous
  replacement, not an exception to the count.
- Stopband: for a `0.001` peak low-level coherent sine, assess both relative gain
  and absolute output-peak error. Propose `0.01 dB` and `0.001 rad` where analytical
  output peak is at least `1e-6`; below that level, propose absolute output-peak
  error at most `1e-7` and report relative dB as diagnostic only. Do not extrapolate
  the existing 48 kHz 1 dB example to other rates or settle-window durations.
- Self-oscillation: after a 1.0-peak impulse at 1 kHz cutoff and zero drive, require
  the last 0.5 seconds of a 2-second render to peak below `1e-5` at resonance 0.98,
  and to exceed `1e-4` peak and RMS at resonance 1.0, with positive-crossing
  frequency within 1% of 1 kHz. Repeat at floor, mid and ceiling cutoff; **do not
  apply the 1 kHz frequency target to those other cutoffs**. A separate 10 Hz,
  30-second Off-path impulse proposal is recorded above for review, not acceptance.
  Set ceiling-cutoff, quality-path, true startup and spectral-purity limits
  from measured data before acceptance.
- IMD: measure coherent two-tone fundamentals, third-order products and
  unattributed residual against the *same-input, same-tap* converged reference.
  Proposed comparison bounds are `0.03 dB` per fundamental and `1e-3` absolute
  peak amplitude per IMD product at input peaks `0.25` per tone. This is an error
  bound versus the model, **not** a maximum permissible creative distortion;
  sweep level, spacing, cutoff and drive before freezing it.
- Aliasing/modulation: measure coherent, settled non-harmonic bins against an
  adequately band-limited higher-rate render, after compensating quality-path
  latency and matching the fundamental. Propose alias spur peaks below `-80 dBc`
  for 4x and below `-60 dBc` for 2x on a 0.5-peak, +12 dB driven tone; characterize
  Off explicitly rather than applying an oversampled bound. For modulation,
  compare expected sidebands to the converged, identically modulated reference;
  propose maximum unexpected spur `-60 dBc` relative to the largest expected
  component. Verify at high resonance and overload; revise the figures if the
  model or documented listening contradicts them. Never classify intentional
  harmonics or expected modulation sidebands as alias spurs.
- Release cost: benchmark the **complete** candidate render/processor path on
  the physical M1 Pro. For 1x, Thomas approved the four-combination
  44.1/48 kHz × 128/257 samples × eight-voice 30-second target above, not a
  measured pass. For 2x/4x/8x the real-time operating envelopes and CPU
  targets remain proposed; measure exposed boundary cases before approving
  restrictions. Verify offline rendering safety for retained modes separately
  from real-time deadlines. Report median, p99.9, maximum, latency, per-voice cost
  and a legacy comparison. Isolated solver or filter timing cannot pass the
  complete-processor gate; offline simulated exceedances are not actual
  audio-device callback misses.

**Listening method revised by Thomas on 27 September 2026:** The blinded,
randomized, level-matched three-repeat protocol approved on 26 September is
superseded as a mandatory gate. Thomas's firsthand unblinded coupled 1x
audition establishes a positive sound preference, not verification of gain,
resonance, overload, modulation or the retained higher-quality paths.
Document unblinded checks on representative patches and settings for
retained 1x/2x/4x/8x on their intended cases. The development coupled preview
can exercise all four factors for those checks.
Include bass, near-resonant sweeps, self-oscillation startup/ringdown,
high-register drive, two-tone intervals and independently/combined-modulated
controls across representative rates. Record patch, quality, rate, controls,
monitoring conditions, audible differences/artifacts, gain/headroom observations
and preference; review aliasing, zippering, clicks, level loss and instability.
Retain raw renders for gain and headroom audit. Level/latency matching and
blinded comparisons are optional diagnostic aids if the sound decision is
uncertain, not prerequisites. Thomas is the named product reviewer and must
approve the scoped listening results, numerical limits and CPU target in
ADR 0005 before production integration. Missing representative checks or
sign-off are not implicit passes. No higher-mode result is recorded yet.

## Required Handoff Scope

The handoff target is one coherent, measured and auditioned Mono engine. It requires:

- an authoritative ladder model and absolute-reference validation;
- a deliberate oscillator/mixer/filter/VCA gain structure;
- a final contour and monophonic articulation model;
- independent time-varying oscillator movement and defined phase behavior;
- measured, sample-rate-stable noise;
- separately validated oscillator and nonlinear alias performance;
- bounded real-time processing and acceptable Release CPU use;
- a final parameter/UI contract, new current-only schemas and rebuilt presets; and
- deterministic tests, generated evidence and documented listening approval.

Classic oscillator-3/noise modulation through the wheel must be explicitly accepted
as required or excluded when the product contract is frozen. It must not remain an
implicit aspiration. If required, oscillator 3 remains available as a modulation
source when its audio-mixer level is zero, and modulation is processed at audio rate.

## Deferred Or Separately Gated Scope

The following do not block the core unless the product contract deliberately promotes
them to required scope:

- a dedicated LFO;
- external audio input;
- an internal output-to-input feedback feature;
- exact component tolerances, device noise and temperature behavior;
- oversampling above 4x in production; and
- a hardware-accuracy claim.

There is no planned permanent legacy-filter mode. Retain the old filter temporarily
only as a development and listening baseline, and remove it if the selected core is
accepted. A feature is not retained merely because an existing preset uses it.

## Plan Of Record

The provisional pre-alpha development ladder may be exercised in Mono's
processor before ADR 0005 is Accepted. The phases below describe the
acceptance/release handoff, not a prohibition on development integration.
Track provisional results and outstanding gates in
`docs/MONO_LADDER_ACCEPTANCE_PLAN.md`.

On 27 September 2026 the one-step candidate was linked into `VektMonoCore`
and may be enabled only via an explicit local C++ development constructor at
1x. Normal host-created instances and higher quality settings remain on
the legacy ladder; neither the current parameter schema nor the offline
16x host policy has changed. The latest focused development render,
processor and UI selection passes 178,490 assertions in six cases. A short
48 kHz/257-sample/eight-voice, 1x complete-processor Release diagnostic
measures candidate/legacy medians of about
2268/1102 us over ten callbacks, with zero *simulated* deadline exceedances.
An additional candidate-only two-second probe at the same setting recorded
374 callbacks, median 2202.958 us and p99.9/max 4667.917 us, with zero
simulated deadline exceedances. That p99.9 **exceeds** the approved 75%
headroom target of 4015.625 us against the 5354.167 us block deadline;
it is not the required 30-second candidate/legacy qualification.
This does not establish processing-thread allocation safety, callback tails,
the approved 30-second CPU target or acceptable sound. Two engine-labeled,
unblinded 48 kHz filter-sweep and envelope render pairs were generated under
`/tmp/vekt-provisional-audition/` but have not been listened to. Their
candidate/legacy left peaks are approximately 0.537/0.360 and 0.352/0.141;
level matching and headroom need review before sound conclusions.
An experimental coupled Newton solve of the **same four implicit stage
equations** has been added alongside the nested implementation and is not
selected by default. Focused five-rate, modulated/overloaded,
near-cutoff-ceiling and settled 48 kHz host-bin comparisons against nested
and the one-step offline reference pass 123,366 assertions in four cases,
including abrupt controls and seeded input. None of those cases reports
fallback or non-finite samples, and measured maximum residual is at most
`2e-7`.
In a verified sequential 30-second Release complete-processor diagnostic
(48 kHz, 257-sample blocks, eight active voices, 5,604 callbacks per mode),
nested/coupled medians were `2125.167/1175.166 us`, p99.9 times were
`4618.500/1355.958 us`, maxima were `6164.333/1379.542 us`, and simulated
deadline exceedances were `3/0`. The coupled path met the approved
`4015.625 us` p99.9 headroom target with zero simulated exceedances for
**this case only**; the nested path did not. Coupled-only 30-second runs at
44.1 kHz/128 samples (10,336 callbacks) measured p99.9 `1984.291 us`
against a 75%-deadline target of approximately `2176.871 us`, but had two
simulated deadline exceedances (maximum `5204.750 us`), so **failed**. At
44.1 kHz/257 samples (5,148 callbacks), p99.9 was `1377.750 us` against
`4370.748 us`, with zero exceedances (maximum `1435.000 us`). At
48 kHz/128 samples (11,250 callbacks), p99.9 was `3639.292 us` against
`2000.000 us`, with 19 exceedances (maximum `11213.958 us`), so **failed**.
All three runs reported zero latency and exited successfully. The tests
and timings neither establish processing-thread allocation safety nor
complete reference/alias coverage. Two of the four approved combinations
miss the approved timing rule; no full CPU/safety gate passes, and the
default solver remains nested. Investigate the callback-tail outliers before
reconsidering solver selection.
In isolated repeats with development-only solver-work counters (30 seconds,
eight voices, 1x), 48 kHz/128 yielded median `585.375 us`, p99.9
`735.833 us`, maximum `2474.583 us` and **zero** simulated exceedances
over 11,250 callbacks; its earlier run had 19 exceedances. The 44.1 kHz/128
repeat yielded median `600.583 us`, p99.9 `2126.417 us`, maximum
`3238.208 us` and **four** simulated exceedances over 10,336 callbacks;
its earlier run had two. Both exits were zero and reported no solver
fallback/non-finite samples. Their slowest callbacks used 4093 and 4095
coupled Newton iterations respectively for 2048 processed ladder samples,
near the approximately two-iteration-per-sample run average. This does not
attribute the timing spikes to scheduling or prove a processing-thread
allocation count. Timing outcomes vary; the approved zero-exceedance rule
remains unmet at 44.1 kHz/128. A subsequent run using over-deadline
work aggregation is recorded below; its counters do not explain the spike.
With per-callback work aggregation, another isolated 44.1 kHz/128 run
(10,336 callbacks, exit 0) had p99.9 `1904.583 us`, maximum `3336.750 us`
and one simulated deadline exceedance. That callback used 4096 iterations
and line-search trials for 2048 ladder samples, about two iterations per
sample, and had no reported fallback. In a subsequent sequential matched
legacy/coupled run (both 30 seconds, exit 0) at the same setting, legacy
median/p99.9/max were `521.500/1322.875/3446.000 us` with two simulated
exceedances; coupled was `589.167/1771.709/2708.667 us` with zero. The
coupled slowest callback also used two Newton iterations per ladder sample.
These runs show offline timing variability in *both* engines, not proof that
solver work or scheduling caused the spikes or that the CPU gate passes.
Diagnostic snapshots were outside the timed callback but can still affect
between-callback scheduling. Processing-thread allocations remain unmeasured.
An executable-local C++ `new`/`new[]` probe now verifies that it detects a
deliberate allocation, then counts calling-thread `new` calls during each
`processBlock`. Isolated 30-second coupled runs at 44.1/48 kHz, 128 samples
and eight voices measured **zero covered `new` calls** in 10,336/11,250
callbacks. The 44.1 kHz run had p99.9 `789.084 us`, maximum `3156.208 us`
and one simulated deadline exceedance; the 48 kHz run had p99.9
`1899.791 us`, maximum `4933.250 us` and two exceedances. Both exited
successfully with zero reported solver fallback/non-finite samples. This
probe misses direct `malloc`, other platform allocators and other threads;
its off-timer snapshots may also perturb scheduling. It is not proof of
complete processing-thread allocation safety. Neither run meets the
approved zero-exceedance rule, and ordinary output/listening acceptance
remains open. The full development suite against the earlier work-instrumented
coupled-prototype binary passed 256 cases and 7,002,048 assertions (exit 0);
the allocation probe is compiled into the standalone cost tool, not that
development test executable.
After narrowing the probe window to `processBlock` only, a further isolated
44.1 kHz/128-sample, eight-voice coupled run completed 10,336 callbacks
(exit 0). Median/p99.9/max were `589.458/805.125/2933.875 us` against a
`2902.494 us` simulated deadline; **one** callback exceeded it. The verified
probe recorded zero covered calling-thread C++ `new` calls. That callback
used 4091 coupled Newton iterations and line-search trials for 2048 ladder
samples, without reported solver fallback/non-finite samples. This corrected
run still fails the approved zero-exceedance rule; neither the cause of its
outlier nor complete processing-thread allocation safety is established.
The allocation-count window is limited to `processBlock`; reported elapsed
time also includes probe begin/end calls. The strengthened Release self-test
detects scalar `new`, `new[]` and aligned `new` with three deliberate
allocations. In subsequent sequential 30-second coupled-only eight-voice
257-sample runs, 44.1 kHz (5,148 callbacks) measured median/p99.9/max
`1190.833/4221.084/7498.083 us` and **one** simulated exceedance against
`5827.664 us`; 48 kHz (5,604 callbacks) measured
`1176.625/1780.334/1972.125 us` and zero exceedances against `5354.167 us`.
Both runs exited successfully, reporting zero covered callback C++ `new`
calls and zero solver fallback/non-finite samples. The 44.1 kHz/257 run
fails the zero-exceedance rule despite its p99.9 being under the approved
`4370.748 us` headroom threshold. This does not invalidate earlier passing
runs but shows tail variability at the larger block size too; it does not
prove the cause or complete allocation safety.
The Release cost tool now defaults to a final-only solver-work snapshot for
coupled runs; a trailing `work` argument explicitly enables per-callback
diagnostics. In sequential 30-second 44.1 kHz/128, eight-voice, 1x coupled
runs (10,336 callbacks each, exit 0), final-only median/p99.9/max was
`591.166/3117.583/25880.083 us` with **11** simulated exceedances; the
per-callback mode measured `594.209/957.458/3353.458 us` with **one**, against
the `2902.494 us` deadline. Both reported identical output sum of squares
(`231268.160`) and coupled work totals (21,241,856 samples; 42,467,693
iterations and line-search trials), zero covered callback C++ `new` calls,
and zero reported fallback/non-finite samples. The per-callback overrun used
4085 iterations for 2048 ladder samples. Both modes fail zero exceedances;
the sequential offline comparison neither establishes controlled scheduling
nor attributes timing outliers to diagnostic traversal, allocator calls or
solver work. Elapsed measurements still include allocation-probe begin/end.
The cost tool now reports `measured_timing_rules_met` separately from its
execution exit code. It is 1 only if unrounded p99.9 is strictly below 75% of
the simulated block deadline and the run has zero simulated exceedances.
The strict-boundary rule test plus coupled focused tests passed 123,380
assertions in five cases. Candidate and ordinary Release tools built; a
short ordinary legacy 16x smoke run exited 0 while reporting
`measured_timing_rules_met=0` (19/19 simulated exceedances), confirming exit 0
is not a timing qualification. That smoke run is not an approved 1x-candidate
measurement. The field does not assert solver fallback safety, complete
allocation coverage, sound equivalence, host scheduling or the full gate.
Earlier timing rows predate this field; they were not silently reclassified.
An additional development-only nested-versus-coupled processor test compares
full stereo callbacks at 44.1/48 kHz and 128/257 samples with multiple voices,
mid-block note-on/off events and cutoff/resonance/drive changes. All four
configurations remained finite with nonzero output, equal latency and maximum
absolute sample differences below `1e-4`; coupled reported zero fallback and
non-finite samples. The coupled-focused selection passed 197,520 assertions
in five cases. This is deterministic numerical evidence, not a listening or
complete spectral pass, and it does not repair the CPU/safety failures. The
full development suite with this comparison passed 258 cases and 7,076,204
assertions (exit 0). During validation, do not issue five or more consecutive
identical `run_commands` calls to poll a long-running suite; use a single
completion record and varied, bounded verification rather than a polling loop.
An opt-in `transitions` workload was added only to the Release cost tool. MIDI
events are built before the timer and calling-thread C++ `new` counter starts;
their handling is measured inside `processBlock`. The sustained-note workload
is unchanged. Sequential 2-second nested/coupled development runs at
44.1 kHz/128 samples and **16 voices** each completed 690 callbacks, 87 with
note transitions (exit 0). Both self-tested the allocation interceptor and
recorded zero covered callback C++ `new` calls. Nested/coupled median was
`2740.792/1271.625 us`, p99.9 `3357.667/1550.000 us`, and simulated
exceedances `250/0`; coupled reported zero fallback/non-finite samples.
This workload is **not** the approved eight-voice, 30-second sustained-note
CPU configuration. It provides limited transition-path allocation evidence,
not proof about direct `malloc`, other allocators, other threads, or complete
real-time safety. Its timing result does not qualify the CPU gate.
The cost tool now separates transition versus other callback simulated
exceedances and maximum times. A further sequential 2-second 44.1 kHz/128,
16-voice nested/coupled pair had 690 callbacks each, including 87 transition
callbacks. Nested exceeded the simulated deadline 251 times: 35 transition
callbacks and 216 of the 603 other callbacks. Its maxima were
`3353.667/4196.833 us` with/without transitions. Coupled exceeded neither
category; its maxima were `1421.208/1468.334 us` with/without transitions.
Both rows reported zero covered callback C++ `new` calls and coupled reported
zero fallback/non-finite samples. Split counts and maxima were verified
against the emitted rows and a short sustained-workload regression. This does
not establish that transitions caused the tails or qualify the approved
eight-voice, 30-second CPU/safety gate; the probe's allocator and thread
coverage remains limited.
The executable-local callback probe now covers scalar, array and aligned
nothrow C++ `new` in addition to the previous throwing forms. Its Release
self-test verifies seven allocation forms; both candidate-enabled and ordinary
cost tools built and passed. A short 44.1 kHz/128-sample, eight-voice coupled
transition run completed 104 callbacks, 13 with note transitions (exit 0),
recorded zero covered callback C++ `new` calls and reported zero solver
fallback/non-finite samples. The ordinary tool rejects the development mode.
A source scan found no explicit `malloc`, `calloc` or `realloc` calls in the
inspected Mono processor, voice or ladder files. Neither that scan nor the
probe covers allocations inside dependencies, all platform allocator APIs or
other threads. This short run is not an approved CPU/safety-gate measurement.
On 27 September 2026, four sequential, approved-configuration 30-second
eight-voice 1x coupled sustained-note Release runs on MacBookPro18,1 used
`caffeinate -i` to avoid idle sleep but otherwise kept the default scheduler.
At 44.1 kHz/128, 44.1 kHz/257, 48 kHz/128 and 48 kHz/257 respectively,
the p99.9 values were `768.916`, `2201.709`, `1178.833` and `6050.209 us`;
the maxima were `966.792`, `9493.750`, `3722.334` and `16419.750 us`;
simulated deadline exceedances were `0`, `2`, `2` and `6`. Thus only the
44.1 kHz/128 run met the *measured timing rule in that run*, while the other
three failed; previous 44.1 kHz/128 runs also failed. All runs exited 0,
had zero covered callback C++ `new` calls with the seven-form probe self-test,
zero reported solver fallback/non-finite samples and zero latency samples.
Nearby `ps` snapshots showed appreciable competing VS Code, WebKit and
WindowServer activity, so this was **not** controlled host scheduling and
cannot attribute outliers to those processes. These offline timings are not
device callbacks; allocator coverage, sound acceptance and the full CPU/safety
gate remain open. The emitted run record is at
`/tmp/vekt-approved-coupled-20260927.log` (ephemeral local evidence).
The earlier full development suite with the new rule test passed 257 cases and
7,002,056 assertions (exit 0); this does not qualify the full product gate.
An earlier full development suite passed 256 cases and 7,002,048 assertions against
the work-instrumented plugin binary. The later probe-window edit was limited
to the cost executable; both its candidate-enabled and ordinary Release
variants built. Its deliberate-allocation self-test passed, but this does not
extend the probe to direct `malloc` or work on other threads.
The development-only standalone preview builds with its own product name and
bundle ID `com.vekt.mono.ladderpreview`, and uses a separate user-preset
directory; it is not installed, launched or auditioned. Normal Release VST3
builds with the development option off and continues to instantiate legacy.
The preview editor visibly distinguishes `DEV CANDIDATE 1x` from
`DEV PREVIEW: LEGACY` at higher factors; its isolated UI test passes
without JUCE assertions. The focused render/processor/UI selection passes
178,490 assertions in six cases. At that earlier preview increment, the full
development suite passed 252 cases and 6,878,676 assertions (exit 0). These
checks do not establish a listening result, allocation safety or Release
CPU qualification.
The engine-labeled WAVs alone do not establish gain/headroom or control-specific
listening results. Collect subjective observations
from the standalone preview only from a human reviewer; at this earlier
preview increment no audition had yet been recorded. The subsequent 1x
coupled audition is recorded above.

### 0. Baseline The Development Increment

- Build and run the staged test suite before further engine changes.
- Generate the current deterministic Mono and ladder reports in Release mode.
- Record the compiler, architecture, rates, block sizes and fixture seed.
- Use current output as comparison evidence, not as a compatibility requirement.
- Do not stage, commit or discard unrelated work as part of this plan.

**Gate:** the starting tree and generated evidence are reproducible.

### 1. Freeze The Product Contract

Divide targets into three explicit categories:

- **Circuit-derived:** topology, state equations, nonlinear functions, internal
  scaling, feedback node, output tap and continuous-time small-signal behavior.
- **Behavioral:** cutoff tracking, resonance buildup, self-oscillation, modulation,
  articulation, drift, quality and stability.
- **Sound-design:** parameter tapers, maximum drive, optional compensation, output
  trim, polyphony, unison and stereo behavior.

Resolve whether classic modulation, low-note priority, nonlinear mixer/VCA behavior,
Q compensation and output feedback are required, optional or excluded. Current
parameter identifiers, ranges and tests do not constrain these decisions.

**Gate:** every feature is classified and every required behavior has a testable
contract or a later phase responsible for defining one.

### 2. Select The Authoritative Ladder Model

Add a Mono ladder decision record specifying:

- the continuous-time equations for all four stages;
- each nonlinear function and its physical or normalized scaling;
- input, feedback-node and output scaling and polarity;
- the cutoff and normalized-resonance definitions;
- the intended self-oscillation threshold;
- discretization, prewarping and oversampling assumptions;
- the nonlinear solver, iteration bound, convergence test and fallback; and
- intentional simplifications and exclusions.

Huovilainen and Zavalishin may inform the decision, but the record must identify the
exact combined model being implemented. Zero-delay feedback is an architectural
property, not evidence of authenticity by itself.

**Gate:** equations and scaling conventions are frozen before candidate tuning.

### 3. Extract Development-Only Audio Analysis

Create a target such as `vekt::audio_analysis` outside production `vekt::dsp`.
Generalize deterministic stimuli, measurement windows, RMS/peak/DC/crest metrics,
complex sinusoidal projection, gain/phase response, harmonic and IMD components,
ringdown, alias residuals, level matching, sample/block comparison and typed results.

Keep Mono note scenarios, parameter matrices, patch definitions, acceptance limits
and JSON report composition product-local. Refactor existing Audio Lab and processor
test helpers where this removes duplication, but do not create a generic synthesizer
framework or duplicate product DSP in the harness.

**Gate:** current reports remain semantically equivalent, determinism and block-size
tests pass, and no production target depends on the analysis module.

### 4. Add Absolute Filter References

Implement both:

1. a complex analytical low-level response derived from the selected model; and
2. a double-precision offline nonlinear reference using smaller time steps or high
   reference oversampling, tighter tolerances and progressively verified convergence.

Use the references to validate DC gain, stage and aggregate response, phase,
prewarping, resonance, self-oscillation, nonlinear response and sample-rate behavior.
Freeze numerical tolerances before final candidate tuning. Higher factors such as 8x
or 16x are reference tools, not implied production modes.

**Gate:** the references converge and can reject incorrect equations or scaling.

### 5. Validate And Correct The Candidate Ladder

Validate one dependency at a time: stage equation, nonlinear scaling, global feedback,
TPT update, output tap, cutoff prewarping, resonance threshold, solver initialization,
bounded fallback and oversampling interaction. Cover supported host rates, Off/2x/4x
production candidates, low through high cutoff and resonance, low through overloaded
input, static and rapidly modulated controls, and deterministic silence/impulse/tone/
sweep/noise/two-tone stimuli.

Reject the candidate for non-finite output, unbounded or input-dependent work,
discontinuous fallback, unintended autonomous state below its specified threshold,
excess reference error, block-size dependence or unacceptable Release cost. Do not
hide model errors with output normalization or legacy-derived control remapping.

**Gate:** the candidate passes the analytical, converged nonlinear, stability,
determinism and bounded-cost matrices.

### 6. Design The Complete Gain Structure

Measure and decide oscillator/noise scaling, mixer summing and optional saturation,
ladder input/internal/output scaling, VCA behavior, master output, optional Q or drive
compensation and any optional output-return path. Include single and three-oscillator
levels, pulse DC/coupling, white and pink noise, two-tone IMD, resonance versus input
level, maximum polyphony/unison peaks and silence/tail DC.

Evaluate raw circuit drive separately from any explicit loudness-compensated drive.
The current `output / sqrt(driveGain)` behavior has no preservation priority. Q
compensation remains default-off and outside all intended nonlinear stages if it is
retained, but its current checkbox, name and provisional law may be changed or removed.
Any future output-feedback tap precedes compensation.

**Gate:** the complete path has intentional overload progression, acceptable DC and
headroom, and documented control semantics.

### 7. Integrate The Selected Filter For Release

Replace `DelayedFeedbackLadder` in `MonoVoice`, remove hidden legacy normalization,
delete old-filter tests that encode discarded behavior and regenerate affected
fixtures after reviewing and recording the intended deltas. Do not add a legacy mode,
state migration or legacy cutoff map. Retain historical reports only as development
evidence.

Add production tests for reset, silence, finite output, block/rate invariance, cutoff,
resonance and drive automation, self-oscillation, keyboard tracking, contour
modulation, compensation placement, quality behavior and processor/engine parity.

**Gate:** only the selected ladder remains in production and all final filter
contracts pass.

### 8. Replace Contours And Monophonic Articulation

Compare the current linear JUCE ADSRs and unconditional 3 ms allocation fade with a
capacitor-style contour and targeted discontinuity handling. Define live edits,
single-trigger behavior, retrigger from current level, decay-linked release, optional
modern independent release, sustain transitions, glide law, note priority, held-key
return, bend and mode changes as one state machine.

The staged glide correction is useful evidence but not immutable. Prefer classic
low-note priority for the classic mono contract unless another behavior is selected
deliberately. Remove the blanket fade if targeted transition handling makes it
unnecessary.

**Gate:** fast attacks, repeated notes, legato transitions and release behavior meet
the final contract without unrelated attack softening.

### 9. Correct Oscillators, Drift And Noise

Measure waveform level, DC, fundamental, harmonics, high-frequency limits and alias
residuals. Define intended waveform shapes and phase policy; correct triangle
aliasing or demonstrate that it is below the accepted limit. Separate calibration,
manual detune, unison detune, common movement and independent oscillator drift.
Replace static shared `driftCents` with bounded, deterministic, sample-rate-independent
time-varying oscillator states. Replace one-pole low-passed white "pink" noise with a
measured, sample-rate-stable design.

**Gate:** Drift changes oscillator relationships over time, and oscillator/noise
spectra satisfy the final rate and aliasing criteria.

### 10. Implement Required Classic Modulation

If Phase 1 classifies classic modulation as required, implement oscillator 3 and
noise source mixing through wheel depth to pitch and filter, with explicit laws,
audio-rate processing and suitable oscillator-3 range behavior. Audio mixer level
must not gate the modulation source. A dedicated LFO remains separately optional.

**Gate:** zero-depth null, source levels, polarity, destinations, MIDI control,
audio-rate behavior, state recall and block/rate invariance pass.

### 11. Select Final Quality And Review Real-Time Safety

Compare planned candidate 1x/2x/4x/8x paths against higher-rate offline
references (which may use 16x/32x for diagnostics). Each retained higher-factor
choice still requires measured spectral and listening review; real-time paths
need CPU deadlines and offline claims need bounded rendering validation.
Validate oscillator and nonlinear alias residuals,
resampling-filter contribution, modulation sidebands, self-oscillation, latency and
Release CPU separately.

Review the audio path for allocation, locking, I/O, exceptions, lazy construction,
unsafe reconfiguration and unbounded work. Test representative host rates and
32-sample buffers with maximum voices/unison, all sources, high resonance, maximum
drive, modulation and the highest supported production quality.

**Gate:** the selected quality modes meet aliasing, latency, bounded-work and Release
performance criteria.

### 12. Freeze Parameters, UI, Schemas And Presets

Only after DSP behavior is stable, define final parameter identifiers, order, ranges,
tapers, units, defaults, UI controls and tooltips. Remove obsolete parameters and
tests instead of carrying them for compatibility. Establish one final pre-alpha
project schema and one final sound schema; reject all older Mono schemas without
migration or partial application.

Retune, replace, rename or remove every factory preset against the completed engine.
Each retained preset must store the complete current sound state and pass musical,
peak, DC, articulation, modulation, stereo/unison and category/name review. Old
numeric values and rendered sound are not preservation targets.

**Gate:** every parameter has one precise meaning, current state round-trips, obsolete
state is rejected cleanly and all bundled presets have been auditioned.

### 13. Listening Approval And Final Handoff

Use fixed-seed raw renders and optional level-matched renders of the legacy
baseline, raw validated candidate, explicitly compensated candidate where
applicable and completed signal path. Representative listening checks cover
single-oscillator bass, three-oscillator bass, resonant pluck, fast repeated
bass notes, legato lead, bright upper-register lead,
cutoff sweep, resonance/drive interaction, self-oscillation with tracking, required
modulation and feedback only if implemented.

Report any matching gain, audition unblinded in solo and backing contexts as useful,
and do not approve a candidate merely because it is louder, brighter, wider, more
distorted or numerically more elaborate. Objective validation and a documented
musical advantage or equivalence with lower technical risk are both required;
document the judgment without a mandatory repeat-count protocol.

Run the complete Debug/Release, plugin-format, sample-rate, block-size, quality,
performance-mode, state/preset, automation, silence/non-finite, stress, CPU and Audio
Lab matrices. Remove the legacy ladder, migrations, obsolete parameters/UI/presets,
temporary remapping experiments and tests tied only to discarded behavior. Retain
the model decision, analysis infrastructure, absolute references, final reports,
selected renders and documented limitations.

**Gate:** the handoff package is complete and the repository contains no accidental
legacy path.

## Definition Of Ready For Handoff

Mono is ready for handoff when:

- no production decision exists solely to preserve pre-alpha behavior;
- circuit-derived, behavioral and sound-design requirements are separated;
- the selected ladder passes analytical and converged nonlinear references;
- no legacy-response remapping is used as an authenticity target;
- the complete gain structure and compensation placement are intentional;
- contours and performance follow one documented state machine;
- drift is independent and time-varying and noise is rate-stable;
- required modulation is implemented or explicitly excluded from scope;
- production work is deterministic, finite, bounded and real-time safe;
- final parameters describe the completed engine rather than transitional experiments;
- earlier project/preset schemas are rejected rather than migrated;
- every factory preset is rebuilt and auditioned;
- fixtures represent final contracts and all validation matrices pass; and
- the legacy filter is removed unless it has earned a separately documented musical
  role, in which case that role is not described as compatibility.
