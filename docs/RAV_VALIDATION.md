# Vekt Rav Validation

This document defines the repeatable quality gates for the five-mode Rav processor.

## Per-sample precision (3 October 2026)

ADR 0010 step 9, ARCHITECTURE.md (DSP Contracts). The fuzz circuit's couplings and bias recovery (3-143 Hz) and the
mode stage's saturation feedback, overdrive high-pass and gated-fuzz envelope are double; in float they were up to
-86 dBFS off at 192 kHz x16 (a scratch float/double one-pole measurement). The post stage (4-18 kHz) and fuzz tone
filter (about 2.4 kHz) stay float. `ControlTransition` now ramps in double: in float a bias move on the 1 s
operating-point ramp held still and then stepped. The references changed (gated fuzz -47 dBFS, circuit fuzz -66 dBFS,
the other cases -85 to -92 dBFS; with only `ControlTransition` back in float just `multiband-tone-mix` still differed,
-87 dBFS, from the tone and auto-gain ramps) and were recaptured (approved). Release cost at 16x tracking, 48 kHz,
256-sample blocks, one 10 s run each (`VektRavRender --profile tracking --quality 6 --mode <m>`): Saturation 16.1 % to
17.5 %, Circuit Fuzz 24.4 % to 27.4 %, Gated Fuzz 14.9 % to 17.2 % of a core, float (HEAD) to double. The per-sample
coefficient transcendentals are the cost. Since step 10 (same day) each is recomputed only when its control changes
(`RavCachedCoefficient`); the output is bit-identical (references at zero tolerance), and `Rav mode stage recomputes
its cached coefficients after a rate change` pins the invalidation. The same screen then gives Saturation 17.1 %,
Circuit Fuzz 22.1 %, Gated Fuzz 12.8 %: the fuzz modes are below the float figures, Saturation stays about 6 % above
(its remaining cost is outside the cached coefficients). These are single 10 s screens, not the 30 s timing gates, with
static controls: while Dynamics or Texture ramp every sample still recomputes, so the step 9 figures above are the
worst case (not measured under automation).

## Automated gates

Configure the Debug Ninja preset and run:

```sh
cmake --preset dev
cmake --build --preset dev --target vekt_dsp_tests VektRav_VST3 VektRav_AU
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

Host validation remains separate: run `pluginval` at strictness 10, `auval -v aufx Ravv Vekt`, and DAW smoke tests in Ableton Live, Logic Pro, and GarageBand against the matching installed AUv2 component and VST3 bundle. Signing and installation require separate approval; see [macOS release gates](../packaging/macos/README.md).

## Development Audio Lab

The first Audio Lab milestone is a deterministic headless renderer. It is
opt-in and does not modify the production Standalone, VST3, or AUv2 targets.

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

### Mode and stage selection

Use `--stages` as a five-character enable mask in Saturation, Overdrive,
Distortion, Circuit Fuzz, Gated Fuzz order (for example, `00110`), and
`--stage-order` with a permutation such as `3,1,0,2,4`. `--mode` takes 0–4 in
the same order. `--input path` reads a mono or stereo file into
memory and loops it deterministically. Its source sample rate must equal
`--sample-rate`; the renderer intentionally does not resample comparison input.
`--input-gain` applies the plugin's existing input-gain parameter.

Run performance comparisons in Release mode:

```sh
cmake --preset audio-lab-release
cmake --build --preset audio-lab-release --target VektRavRender
build/audio-lab-release/tools/audio_lab/VektRavRender \
  --source two-tone --profile tracking --quality 2 --mode 3 \
  --stages 00010 --block-size 32 --warmup 0.2 --seconds 5 \
  --report /tmp/rav-circuit-fuzz.json
```

The report contains left/right RMS, peak, and DC, plus mean and maximum measured
block times. Compare candidates with the same build, inputs, parameters, quality,
block size, warmup, and duration. A candidate must be level-matched externally
or with fixed output gain; do not use Auto Gain as the comparison matcher.

Live device input/output, capture, and hardware loopback are future Audio Lab
phases and are not enabled by this renderer.

## Live Audio Lab

The live lab is a separate opt-in application and does not modify the plugin
Standalone, VST3, or AUv2 targets. Launch it with:

```sh
./scripts/run-audio-lab.sh
```

Choose Sine, Sawtooth, Sweep, Impulse, Noise, Kick, Unison, or Two Tone in the safety toolbar and use
the embedded full Rav editor for mode and processing controls. Output starts
muted and must be explicitly armed. The lab uses the default macOS audio
device and does not connect hardware input to output automatically. The
production mode and stage controls select the active processing path.
