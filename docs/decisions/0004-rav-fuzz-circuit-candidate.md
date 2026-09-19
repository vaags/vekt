# ADR 0004: Rav Fuzz Circuit Candidate

## Status

Experimental

## Context

Rav's Legacy Fuzz mode uses an envelope-controlled starvation value, a smooth
gate, and a hard clipper. It is stable and intentionally characterful, but its
interactions are hand-designed rather than based on coupled circuit state.

The first circuit-informed experiment must preserve existing sessions and meet
the audio-thread contract: no allocation, locking, I/O, exceptions, or
input-dependent unbounded work.

## Decision

`RavProcessingModel::fuzzCircuitCandidate` selects `RavFuzzCircuit` only when
the active Rav mode is Fuzz. It is a development-only selection: it is not an
APVTS parameter and is not serialized in presets or host state. Legacy remains
the production default.

The candidate is a fixed-cost, transistor-inspired two-stage topology, not a
component-identical emulation of a named pedal:

1. An input coupling capacitor removes the slowly varying input component.
2. A centred `tanh` transfer approximates the first transistor stage around a
   movable operating point.
3. A rectified excitation estimate charges a bias-recovery state. That state
   shifts the first and second operating points to create dynamic starvation.
4. An interstage coupling capacitor AC-couples the first stage into a second
   centred `tanh` stage.

Each capacitor or recovery node uses the stable one-pole update:

```text
state[n] = state[n-1] + a * (input[n] - state[n-1])
a = 1 - exp(-2 * pi * f / sampleRate)
```

The transfer for each transistor-inspired stage is centred at its operating
point to avoid an unconditional DC offset:

```text
y = tanh(x + bias) - tanh(bias)
```

Control mapping is intentionally bounded:

- **Drive**: input excitation into the candidate.
- **Bias**: initial operating-point offset.
- **Shape**: first-stage gain, asymmetry, and starvation sensitivity.
- **Dynamics**: input-coupling and bias-recovery frequency.
- **Texture**: interstage coupling, second-stage gain, and starvation depth.
- **Tone**: existing Rav Fuzz post-tone behavior; it is held outside the
  candidate so comparisons isolate the nonlinear circuit section.

## Consequences

- The candidate has three state values per stage instance and a bounded number
  of elementary operations per sample.
- It responds differently from Legacy Fuzz and therefore cannot replace it
  without listening, CPU, aliasing, and compatibility approval.
- The existing oversampling paths remain active. Circuit-informed state does
  not eliminate nonlinear aliasing.
- The model intentionally excludes pickup/source impedance, detailed transistor
  device equations, component tolerances, temperature, noise, and a nonlinear
  numerical solver. Those may be investigated only if this reduced model shows
  a measurable and repeatable advantage.

## Evaluation gates

- Finite output and deterministic reset across the supported render matrix.
- Rate-consistent coupling and recovery behavior.
- No processing-thread allocation or unbounded iteration.
- A level-matched listening advantage or a clearly useful new character over
  Legacy Fuzz, measured with the same multiband context and oversampling path.
- Acceptable Release performance at 32-sample buffers with the candidate in all
  three bands and both channels.