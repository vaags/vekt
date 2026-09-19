# Vekt Rav Validation

This document defines the repeatable quality gates for the four-mode Rav processor.

## Automated gates

Configure the Debug Ninja preset and run:

```sh
cmake --preset dev
cmake --build --preset dev --target vekt_dsp_tests VektRav_VST3
ctest --preset dev --output-on-failure
```

The automated suite covers:

- Four-mode finite rendering through the complete processor.
- All seven tracking and offline quality paths.
- Three-band crossover reconstruction and cutoff bounds.
- Base-rate-invariant Bitcrush hold timing.
- Bypass and reported-latency behavior.
- Preset/state round trips and factory schema validation.
- Static and mode-aware Auto Gain constraints.

## Offline render report

For each sample rate `{44100, 48000, 96000, 192000}`, render these inputs:

- 1 kHz sine at -18 dBFS.
- Full-scale impulse.
- Logarithmic sine sweep.
- Stereo-unequal noise.

Run every Rav mode with a fixed, 100% wet setting and Auto Gain off. Compare all seven
quality paths using the same source, warmup, parameters, block size, and build type.
Record:

- Peak and RMS level error after Auto Gain.
- Reported latency and measured impulse displacement.
- Harmonic and non-harmonic spectral components from coherent sine/two-tone renders.
	Treat non-harmonic energy as a comparison metric, not an aliasing claim without a
	higher-rate reference and bin attribution.
- Passband deviation and crossover reconstruction error.
- Bitcrush hold-transition positions in base-rate samples.
- Preparation time, peak memory, and sustained CPU at 32-sample buffers.

## Acceptance criteria

- All outputs are finite; no NaN, Inf, denormal, allocation, lock, or file I/O occurs in warmed-up processing.
- Bitcrush hold transitions are invariant to oversampling factor within resampling edge effects.
- Low + mid + high crossover outputs reconstruct the input within the crossover test tolerance.
- Auto Gain never exceeds unity compensation and its measured loudness error is documented per mode.
- Switching quality updates reported latency and preserves bypass alignment.
- 8x and 16x FIR are available for offline rendering but are not selected for tracking by default.

Host validation remains separate: run `pluginval` at strictness 10, `auval`, and DAW smoke tests in Ableton Live, Logic Pro, and GarageBand after signing the generated bundles.

## Development Audio Lab

The first Audio Lab milestone is a deterministic headless renderer. It is
opt-in and does not modify the production Standalone, VST3, or AUv3 targets.

Run it with:

```sh
./scripts/render-report.sh --source sine --profile tracking --quality 2 --mode 0 \
	--warmup 0.2 --seconds 1 --spectrum-size 32768 \
	--report /tmp/rav-saturation.json --wav /tmp/rav-saturation.wav
```

Sources are `sine`, `sawtooth`, `sweep`, `impulse`, `noise`, `kick`, `unison`,
and `two-tone`. Modes use the numeric order `0` through `3`: Saturation,
Overdrive, Distortion, and Fuzz. The renderer also accepts `--profile`,
`--quality`, `--warmup`, `--frequency`, shaping/mix parameters, `--seed`,
`--spectrum-size`, `--report`, and `--wav`.

The renderer invokes the real processor in the selected tracking or offline
context and reports sample count, RMS, peak, DC, active quality, plugin latency,
and warmup-excluded processing time. `--spectrum-size` adds a Hann-windowed FFT
peak report. Peaks above 0 dBFS are intentionally reported rather than limited
so aggressive mode behavior remains visible.

### Circuit-model comparison workflow

`--model` selects the development comparison seam: `legacy`, `behavioral`,
`overdrive-circuit`, or `fuzz-circuit`. The seam is deliberately excluded from
APVTS, plugin state, and presets. `fuzz-circuit` is the implemented experimental
candidate and reports `active_model: "fuzz-circuit"` when an active Fuzz stage
uses it. The behavioral and overdrive-circuit placeholders report
`active_model: "legacy"`; this makes baseline reports reproducible without
changing saved sounds.

Use `--stages` as a four-character enable mask in Saturation, Overdrive,
Distortion, Fuzz order (for example, `0011`), and `--stage-order` with a
permutation such as `3,1,0,2`. `--input path` reads a mono or stereo file into
memory and loops it deterministically. Its source sample rate must equal
`--sample-rate`; the renderer intentionally does not resample comparison input.
`--input-gain` applies the plugin's existing input-gain parameter.

Run performance comparisons in Release mode:

```sh
cmake --preset audio-lab-release
cmake --build --preset audio-lab-release --target VektRavRender
build/audio-lab-release/tools/audio_lab/VektRavRender \
  --model legacy --source two-tone --profile tracking --quality 2 --mode 3 \
  --stages 0001 --block-size 32 --warmup 0.2 --seconds 5 \
  --report /tmp/rav-fuzz-legacy.json
```

The report contains left/right RMS, peak, and DC, plus mean and maximum measured
block times. Compare candidates with the same build, inputs, parameters, quality,
block size, warmup, and duration. A candidate must be level-matched externally
or with fixed output gain; do not use Auto Gain as the comparison matcher.

Live device input/output, capture, and hardware loopback are future Audio Lab
phases and are not enabled by this renderer.

## Live Audio Lab

The live lab is a separate opt-in application and does not modify the plugin
Standalone, VST3, or AUv3 targets. Launch it with:

```sh
./scripts/run-audio-lab.sh
```

Choose Sine, Sawtooth, Sweep, Impulse, Noise, Kick, Unison, or Two Tone in the safety toolbar and use
the embedded full Rav editor for mode and processing controls. Output starts
muted and must be explicitly armed. The lab uses the default macOS audio
device and does not connect hardware input to output automatically. The
development-only **A/B: Legacy / A/B: Fuzz Circuit** button switches the active
Fuzz model without changing parameters, presets, or project state. Audio Lab
briefly fades output down and back up around a switch because the models retain
independent internal state; level-match before judging the result.
