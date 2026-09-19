# Glimmer Validation

Vekt Glimmer is a stereo rotary speaker effect with a stereo-preserving signal
path. It provides horn/drum balance, a fixed stereo microphone pair with angle
and distance controls, linked slow/fast rotor speeds, independent acceleration
and deceleration, tone controls, preamp drive, and loudness-triggered Auto mode.

The nonlinear preamp is the only oversampled stage. Auto Gain compares its
post-input-gain reference against that preamp output before cabinet motion, so
it does not normalize the intended rotor modulation or output-gain changes.
The horn and drum pickups use bounded fractional delays with a stable reported
latency; bypass returns the corresponding latency-aligned raw input.
The Mix control blends the latency-aligned post-input-gain dry signal with the
wet cabinet output before Output gain.

## Automated Coverage

- `RotorMotionTests` verifies target timing, phase continuity, and Auto mode.
- `AutoSpeedDetectorTests` verifies sensitivity thresholds, stereo linking,
  hysteresis, and dwell time.
- `GlimmerParametersTests` verifies the isolated product/preset identifiers and
  quality mapping contract.
- `GlimmerProcessorTests` verifies finite stereo output across every speed mode
  and APVTS project-state restoration, quality profile selection, latency-aligned
  bypass, Mix endpoints, and distance-dependent pickup behavior with stable latency.

## Build Gates

```sh
cmake --build --preset dev --target vekt_dsp_tests VektGlimmer_Standalone VektGlimmer_VST3
ctest --preset dev --output-on-failure
```

Before release, audition organ, guitar, electric piano, stereo pads, and drums
at 44.1, 48, and 96 kHz. Confirm angle wrapping, distance behavior, slow/fast
transitions, Auto-mode hysteresis, bypass, and project-state restoration in a
host. Run available `pluginval`, `auval`, and DAW smoke checks separately.
