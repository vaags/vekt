# ADR 0008: Mono Filter-Output DC Blocking

## Status

Accepted (30 September 2026). Implemented in `MonoVoice` (`filterDcBlockers`, `filterOutputDcBlockerHz`). Mono has
not shipped, so the change deliberately gives up bit identity with the previous Ladder, SVF and K35 renders.

## Context

While integrating K35 (ADR 0007, Integration progress note), the processor measured the output mean at about -18 dB
re RMS for the Ladder and -23 to -29 dB for the SVF at +24 dB Drive. K35 had its own 5 Hz output blocker; the Ladder
and SVF had none, and Mono had no DC blocking anywhere after them.

### Measurements

All at 48 kHz, 1x, over whole periods of the note, in 0.1 s windows from 0.5 s into a held note (hidden tests
`[mono-dc]`, `[mono-dc-source]`, `[mono-dc-processor]`, `[mono-dc-polarity]`, `[mono-dc-thump]`, `[mono-dc-short]`
in `tests/processor/MonoFilterTypeTests.cpp`). "DC" is the mean re the RMS, in dB.

**A true, steady offset.** One controlled saw voice (level 0.7, no tracking, contour or drift, amp sustain 100 %),
notes 33 / 45 / 57, cutoffs 300 Hz / 2 kHz / 5 kHz. The spread of the window means is -75 to -165 dB re RMS, so
this is a constant offset, not slow movement. Ranges over notes and cutoffs:

| Resonance 90 % | Drive 0 | +12 dB | +24 dB |
|---|---|---|---|
| Ladder | -29 to -49 | -11 to -37 | **-9** to -25 |
| SVF | -64 to -90 | -42 to -67 | -36 to -64 |

At Resonance 50 % the Ladder is similar (-11 to -28 at +24 dB) and the SVF worst (-28 to -34 dB at 300 Hz, +24 dB).
At Resonance 0 the Ladder's DC is small at Drive 0 and changes sign with drive. K35 (bare, 2 kHz, Resonance 90 %) is
-42 / -37 / -36 dB at Drive 0 / +12 / +24.

**The ADR 0007 figure mixed DC with beating.** The processor starts with the first factory preset loaded (unison 2x,
three detuned oscillators, delayed vibrato), not with the parameter defaults. With it (A2, Resonance 90 %, 2 kHz),
the mean is -35 / -23 / -19 dB (Ladder) and -54 / -32 / -28 dB (SVF) at Drive 0 / +12 / +24, but the window means
spread by -43 / -29 / -18 and -41 / -25 / -18 dB: comparable to the mean at +24 dB. Detuned layers intermodulate in
the saturation into slow difference products. With a plain saw instead (Osc 1 only, unison 1, no fine tuning or
LFO; key tracking 50 % puts the cutoff near 1.3 kHz), the offset is steady: Ladder -27 / -16 / -13 dB, SVF
-59 / -43 / -43 dB, spread below -90 dB.

**Voices add it coherently.** The offset has the same sign for every note, so a four-note chord is 1-4 dB higher re
its RMS than one note (tones add in power, offsets in amplitude).

### Source

- **Saturation on waveforms without half-wave symmetry.** The oscillator's saw has no DC (-161 dB). Sine, triangle
  and square (half-wave symmetric) give none through any filter (-101 to -126 dB). The saw's DC grows with Drive and
  is largest in the Ladder, which saturates at every stage. An odd nonlinearity has zero mean output only if its input
  distribution is symmetric; a filtered saw's is not.
- **Not a numerical bias.** Each bare filter fed x and then -x gives exactly -y (`y(x) + y(-x) = 0` bit for bit for
  the Ladder, SVF and K35), so the DC flips sign with equal magnitude. At Drive 0, input x 0.1 and x 0.01 lowers the
  DC by 40 dB per decade (Ladder -37 / -74 / -114 dB at Resonance 90 %): the cubic term of the saturation.
- **`WidthDcPolicy::raw`.** A 25 % pulse carries its mean deliberately: -6 dB at the oscillator, -7 dB through the
  Ladder at Drive 0. Zero-centred, only the saturation's part is left (-55 dB at Drive 0, -15 dB at +24 dB).

### Is it heard?

The unblocked DC reaches the output as a pedestal, the mean times the amp envelope. Measured in the 50 ms after note
on and after note off, in the band from 20 Hz to half the fundamental, against the note's own energy in that band
and against the note's whole energy (Ladder and SVF, Resonance 90 %, 2 kHz, notes 33 / 45 / 69 / 81):

- **Note on:** at worst 10 dB below the note in that band and 25 dB below the whole note (Ladder, +24 dB). The note's
  own onset masks it.
- **Note off, 300 ms release:** at least 25 dB below the band.
- **Note off, 5 ms release:** the worst Ladder cases (+24 dB, notes 69 / 81) put the pedestal 6-7.5 dB above the
  note's own sub-fundamental energy, and only 3-6 dB below the whole release. That transient is large enough to be
  potentially audible and dominates the sub-fundamental band during a fast release. No listening test was run.
  The SVF stays at least 18 dB below the band.

The same step appears whenever the offset changes abruptly under a held note: a filter type switch (the 3 ms
continuity ramp turns it into a small thump), a legato pitch change, a voice steal.

## Decision

**Mono lets oscillators and nonlinear filters carry or generate DC internally, and removes DC once, at the common
per-layer filter-output boundary, before amplitude modulation.**

- **Where:** at the start of `MonoVoice::finishSample`, on each unison layer's filter output, whatever the filter
  type, before the amp envelope, pan, layer gain and the continuity offset. `finishSample` is where the Ladder
  (scalar and batched), SVF and K35 paths meet. The type-switch declick (`continuityOffset`) stays after the amp and
  handles audio-rate discontinuities, not steady DC.
- **One state per layer, shared across filter types.** It is not reset on a filter type switch (a reset would itself
  step the output); it resets with the filters (`reset()`, `stop()`), so like them it carries across a voice's natural
  end and restarts deterministically when a voice is reset.
- **K35's own output blocker is folded into it.** It was already on K35's output only (the loop feeds back the
  unblocked limiter output), so K35's model is unchanged; only the ordering with the Resonance trim and the float
  conversion move.
- **Current implementation: first order at 5 Hz** (`vekt::dsp::DcBlocker<double>`, `filterOutputDcBlockerHz`), at
  the voice's rate, including the oversampled one.

### Rejected

- **`WidthDcPolicy::zeroCentered` as the fix.** It would remove the pulse's bias from the filter's input, which is
  part of how a narrow pulse drives the saturation, and would not remove the saturation's own DC.
- **A blocker per filter type** (K35's before this change). Three filters with two DC policies, and a type switch
  would cross between blocker states.
- **Blocking after the amp or on the master output.** The pedestal is already shaped by the envelope there; a high-pass
  cannot remove a fast release's edge, only DC that is left after it.
- **10 or 20 Hz.** It settles faster on short notes (below) but costs the bottom octave: first order at 20 Hz is
  -3 dB at 20 Hz and -1.9 dB with 36 degrees at 27.5 Hz. 5 Hz is the compromise; short notes are a known limit.

## Consequences

- **Audio band:** negligible. First order at 5 Hz:

  | Frequency | Gain | Phase |
  |---|---|---|
  | 10 Hz | -0.97 dB | 26.6 deg |
  | 20 Hz | -0.26 dB | 14.0 deg |
  | 27.5 Hz | -0.14 dB | 10.3 deg |
  | 110 Hz | -0.01 dB | 2.6 deg |

- **Sub-audio output is attenuated:** a filter self-oscillating or closed near its 5 Hz cutoff floor (2.5 Hz with
  modulation) loses 3 dB at 5 Hz and 1 dB at 10 Hz, and slow DC movement (a raw pulse under slow PWM, a slowly
  swept driven filter) is high-passed. Closed-filter leakage at the note frequencies is unchanged (hidden
  `[mono-closed]`: closed SVF -38 to -42 dB at 55 Hz, -58 to -70 dB at 110-220 Hz, as in ADR 0006).
- **Short notes keep part of the offset.** The time constant is 32 ms, so the note-on DC step has fallen to 53 % after
  20 ms, 21 % after 50 ms and 4 % after 100 ms. Worst case (Ladder, +24 dB, Resonance 90 %, 2 kHz, 5 ms attack and
  release), the pedestal in the 50 ms after note off, re the note's own energy in the 20 Hz to f0 / 2 band:

  | Note / length | Unblocked | 5 Hz | 10 Hz | 20 Hz |
  |---|---|---|---|---|
  | 69 / 20 ms | +7.8 | +3.5 | -0.6 | -8.0 |
  | 69 / 50 ms | +5.8 | -7.9 | -17 | -24 |
  | 69 / 100 ms | +5.7 | -22 | -41 | -62 |
  | 69 / 500 ms | +5.8 | -131 | -259 | -468 |
  | 81 / 20 ms | +8.7 | +4.3 | +0.1 | -7.8 |
  | 81 / 100 ms | +7.5 | -20 | -40 | -61 |

  At note on, it is still masked by the note's onset.
- **Bit identity is given up once, for all three filters.** On the `[mono-dump]` fixtures (now with K35 variants):
  Ladder and SVF differ by -18 to -35 dB re the signal, almost all of it below 20 Hz; above 20 Hz the difference is
  -35 to -37 dB, the blocker's phase shift on the lowest harmonics (about -27 dB at 110 Hz, from |H - 1| = 5 / f). The
  output means over the render fall by 6-32 dB (unchanged only for the self-oscillating fixture's SVF at 96 kHz, whose
  mean is not a steady offset). K35 differs only by rounding (-141 to -145 dB re the signal). Audition renders
  change accordingly.
- **CPU:** one first-order recursion per layer per sample; not separately measured.

## Validation

- **Quick suite** (`[dc]`): the blocker's response at 48 and 384 kHz (-3 dB at 5 Hz, within 0.3 dB from 20 Hz);
  every filter's stress case generating DC bare (Ladder -9 dB, SVF -30 dB, K35 -36 dB) and none in the settled voice
  output (-95, -114, -113 dB); the raw 25 % pulse keeping its DC at the oscillator and into the filter while neither
  its voice output nor the zero-centred one carries any, and the two settled outputs still differing by -11 dB re
  RMS (the filter's nonlinear response to the bias stays materially different; not a listening result); a 100 ms note leaving at most -20 dB of its DC step (-26 dB measured). The 20 ms case is not a
  requirement; `[mono-dc-short]` characterises 20 / 50 / 100 / 500 ms.
- **Existing tests:** the quick preset (398 tests), the filter-type and K35 tests (switching declicks both ways,
  switch-gain), and the self-oscillation and low-cutoff tests, slow ones included, all pass unchanged.
- **After the change** (processor, plain saw as above): -92 to -139 dB DC re RMS. With the start-up patch, what is left
  (-46 to -76 dB) moves by as much as the mean between windows: the detuned layers' beating above the blocker's
  cutoff, not an offset.
