# Glimmer Validation

Vekt Glimmer is a stereo-input/stereo-output rotary effect with three models:

- Classic: dual rotors, restrained cabinet bandwidth, contrasting rotor inertia
  and opposite rotation directions.
- Drum: one full-band rotating-baffle path with guitar-oriented speaker voicing.
  Horn Tone and Horn/Drum Balance are inactive but retain their stored settings.
- Wide: dual rotors with broader bandwidth, larger virtual radii and wider mic
  separation. It is not simply a preset of the Width control.

These are designed voicings, not measured emulations of named vintage cabinets.
User tone controls are now shelves rather than flat band gains. Pickup combines
frequency-dependent directivity, amplitude variation and geometry-derived
fractional delay. Angle rotates a model-specific mic pair and affects cabinet
orientation; distance affects directivity and high-frequency loss. Stereo source
channels remain independent before the wet Width stage; there is no room reverb.

The voiced nonlinear preamp is the only oversampled stage. Auto Gain compares its
post-input-gain reference against that preamp output before cabinet motion, so
it does not normalize the intended rotor modulation or output-gain changes.
Reported latency is `round(0.008 * sampleRate) + activeOversamplingLatency`.
The 12 ms delay allocation bound is not host latency. Geometry-dependent travel
differences and filter phase remain part of the effect; mic/model/speed/width
changes do not change PDC. Settled bypass returns latency-aligned raw input,
independent of gain, Mix and Width. Live bypass changes crossfade over 10 ms.
The Mix control blends the latency-aligned post-input-gain dry signal with the
wet cabinet output before Output gain.

## Performance Controls

Brake overrides Manual, which overrides the stored Slow/Fast/Auto selection.
Brake decelerates each rotor to zero and holds its reached phase without muting
the cabinet. Releasing it resumes the effective target. Auto detection continues
during either override and while bypassed. Manual Speed maps 0-100% between
each rotor's Slow and Fast targets, with bounded mechanical slew.

Width is wet-only mid/side scaling: 100% is unchanged, 0% is deliberate mono,
and 200% doubles the side component. Allow headroom above 100%; no limiter is
silently applied. Anti-phase input is retained at normal width and can cancel
when explicitly collapsed to mono.

Gain/drive/tone/balance ramps are 20 ms, mic and width ramps 50 ms, and Mix keeps
its shared 100 ms ramp. Angle uses the shortest wrapped path, including an
interrupted gesture. Live preset recall uses the same transitions. Model changes
warm a second preallocated instance of the same engine for 100 ms, then crossfade
over 50 ms. A newer request waits for the current transition; only the latest
pending model is retained. The editor displays the active/requested transition.

New parameters are appended with version hint 2; existing Speed choice values
and normalized automation mappings are unchanged. Model, Brake, Width, Manual
enablement and Speed position belong to sound presets. Missing new controls in
older project states receive defaults. There is no preset schema migration or
factory preset bank. Runtime phase and RPM are not serialized.

## Renderer

```sh
./scripts/render-report.sh --product glimmer --source two-tone \
  --param glimmer.cabinetModel=Wide --param glimmer.manualSpeedEnabled=true \
  --param glimmer.speedPosition=65 --param glimmer.stereoWidth=125
./scripts/render-report.sh --rack rav,glimmer --param glimmer.cabinetModel=Drum
```

Repeated `--param glimmer.id=value` overrides use native parameter units and
accept choice names or integer indices; booleans also accept true/false.
Unknown, non-finite, fractional-choice and out-of-range values are rejected.
Overrides take precedence over matching legacy flags and work in either rack
order. Legacy rack flags otherwise retain their previous meaning. Glimmer JSON
reports contain effective parameters, active model, pending state, rotor RPM,
latency and measured callback timing. Timing excludes signal generation and file
I/O, but includes OS interruptions; it is not a real-time scheduling guarantee.

## Automated Coverage

- `RotorMotionTests` verifies target timing, phase continuity, signed motion,
  bounded Manual slew and Brake/Manual/Auto precedence.
- `RotaryEngineTests` verifies travel bounds, constant-center latency, Width
  endpoints, full-band Drum routing, inactive horn controls, spectral tone
  response, distinct model responses, side-input preservation and angle wrapping.
- `AutoSpeedDetectorTests` verifies sensitivity thresholds, stereo linking,
  hysteresis, and dwell time.
- `GlimmerParametersTests` verifies the isolated product/preset identifiers and
  quality mapping contract.
- `GlimmerProcessorTests` verifies finite stereo output across every speed mode
  and APVTS project-state restoration, quality profile selection, latency-aligned
  bypass and bypass transitions, linear half-Mix behavior, measured dry impulse
  timing, model request transitions, Brake, legacy project defaults and extreme
  settings at 44.1/48/96/192 kHz with Off/IIR/FIR oversampling.
- Preset cases cover all five new sound parameters and rejection atomicity.
- Editor tests check bounds at 1040/1560/2080 widths and non-overlapping controls.
  Set `VEKT_GLIMMER_SNAPSHOT` to an absolute PNG path for a rendered snapshot.

## Build Gates

```sh
cmake --build --preset dev --target vekt_dsp_tests VektGlimmer_Standalone VektGlimmer_VST3
ctest --preset dev --output-on-failure
```

## Local Validation (2026-09-19)

- Standalone, VST3, AUv3, release Audio Lab and renderer builds succeeded.
- Glimmer Debug VST3 passed pluginval 1.0.4 at strictness 10 with seed 12345,
  GUI tests enabled, default 44.1/48/96 kHz rates and 64-1024 sample blocks.
  The run reported `SUCCESS`, but emitted JUCE assertions in
  `juce_String.cpp:327` (non-ASCII input to the ASCII constructor) and
  `juce_DelayLine.cpp:60` (delay outside its allocated range). These diagnostics
  remain unresolved; this is not an assertion-free release validation.
  The external Steinberg VST3 validator was not configured.
- Targeted `auval -v aufx Glmr Vekt` could not find the registered component;
  AU validation remains blocked despite the successful AUv3 build.
- Full development suite: 135/136 passed. The existing unrelated
  `Rav band wet controls do not couple unaffected bands` failure remains.
- All Glimmer/rotor checks passed, including the 0.5-second tail checks with
  maximum drive/tone and Off/16x FIR quality. Editor snapshots were inspected.
- All three models rendered at 0/50/100% Mix; both rack orders and named/numeric
  parameter overrides passed. Invalid override cases returned exit code 64.
- Opt-in Release benchmark, tracking defaults, 800 callbacks per case, including
  Classic->Drum->Wide->Classic changes: Glimmer used 1.46-3.36% of callback time;
  RAV->Glimmer used 5.64-11.89% at 48/96 kHz and 64/128 samples. No callbacks
  exceeded their budget in that run. Worst measured transition callback was
  0.248 ms or less across the cases. Earlier short renderer runs showed OS
  scheduling outliers, so these measurements are not a dropout guarantee.

To reproduce the opt-in benchmark:

```sh
cmake --preset audio-lab-release -DVEKT_BUILD_TESTS=ON
cmake --build --preset audio-lab-release --target vekt_dsp_tests
VEKT_GLIMMER_BENCHMARK=1 build/audio-lab-release/tests/vekt_dsp_tests 'Glimmer callback benchmark*'
```

## Remaining Release Gates

Resolve the Debug assertions observed during pluginval and repeat validation.
The installed macOS app can be invoked without adding it to `PATH`:

```sh
/Applications/pluginval.app/Contents/MacOS/pluginval \
  --strictness-level 10 --random-seed 12345 \
  --validate "build/dev/plugins/vekt_glimmer/VektGlimmer_artefacts/Debug/VST3/Vekt Glimmer.vst3"
```

Before release, audition level-matched organ, guitar, electric piano, stereo pads, and drums
at 44.1, 48, and 96 kHz. Confirm angle wrapping, distance behavior, slow/fast
transitions, Auto-mode hysteresis, bypass, and project-state restoration in a
host. Run available `pluginval`, `auval`, and DAW smoke checks separately.
Confirm Wide is musically distinct from merely increasing Width. Public pedal
demos are qualitative references, not controlled A/B measurements. Also verify
real-time allocations, drive alias spectra, callback headroom during model
crossfades and the two-product rack, VoiceOver/keyboard interaction, and the
conservative 0.5-second tail under supported extreme settings. Document results
and unavailable tools rather than treating finite-output tests as sonic approval.
