# Vekt Rav Validation

This document defines the repeatable quality gates for the six-mode Rav processor.

## Automated gates

Configure the Debug Ninja preset and run:

```sh
cmake --preset dev
cmake --build --preset dev --target vekt_dsp_tests VektRav_VST3
ctest --preset dev --output-on-failure
```

The automated suite covers:

- Six-mode finite rendering through the complete processor.
- Tracking quality paths: Off, 2x IIR, and 4x IIR.
- Offline profile selection and 16x FIR path construction.
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

Run every Rav mode with tracking `4x IIR`, then compare offline `4x FIR`, `8x FIR`, and `16x FIR`.
Record:

- Peak and RMS level error after Auto Gain.
- Reported latency and measured impulse displacement.
- Alias energy above the input fundamental for nonlinear modes.
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
./scripts/render-report.sh --source sine --seconds 1 --mode 0
```

Sources are `silence`, `sine`, `sweep`, `impulse`, and `noise`. Modes use the
numeric order `0` through `5`: Saturation, Overdrive, Distortion, Fuzz,
Wavefold, and Bitcrush. Additional options include `--sample-rate` and
`--block-size`.

The renderer invokes the real processor in offline mode and reports sample
count, RMS, peak, and plugin latency. Peaks above 0 dBFS are intentionally
reported rather than limited so aggressive mode behavior remains visible.
Live device input/output, capture, and hardware loopback are future Audio Lab
phases and are not enabled by this renderer.
