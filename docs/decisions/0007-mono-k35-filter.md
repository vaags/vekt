# ADR 0007: Mono Third Filter (K35)

> Kobber was called **Mono** until 3 October 2026. Dated entries and records below keep the names of their time:
> Mono, `vekt::mono`, `plugins/vekt_mono`, `Mono*` types, `[mono]` and `[mono-*]` tags, test names "Mono ...", tools
> `VektMono*` (now `VektKobber*`), `VEKT_MONO_*` variables (now `VEKT_KOBBER_*`), and the Audio Lab tools
> `VektRavAudioLab` and `VektRavRender` (now `VektAudioLab` and `VektRender`).

## Status

Accepted (30 September 2026, after the real-synth audition). The K35 core (`NonlinearTptKorg35.h`) and its voicing
(`Korg35Response.h`) are integrated as Mono's third filter (sound schema 12, the `filterK35` parameter,
`LADDER | SVF | K35` in the editor; see Progress). This symmetric reduced model is the baseline; the refinements
under Deferred are experiments against it, kept only if they materially improve the sound.

*Revised 1 October 2026 (pre-release):* K35 is now the third choice of `filterType` (Ladder, SVF, K35) and the
`filterK35` override is removed. The override existed only to keep `filterType`'s two-choice automation mapping
for existing sessions, and none exist before release. A tab click now sets `filterType` alone; switching away from
K35 no longer returns to a Ladder/SVF choice kept underneath. The K35 sound is unchanged (the `mono/k35` reference
render matches). Sections below that describe `filterK35`, the override and its tests are historical.

## Context

Mono has a four-pole nonlinear ladder (ADR 0005) and a two-pole nonlinear SVF in the OTA/SEM family (ADR 0006). A
third filter is worth having only if it is audibly different from both, in ordinary patches and not only in
extreme settings. The brief was an MS-20-inspired filter: "Korg35-inspired, not a circuit emulation".

The first prototype followed that brief literally and abstracted too far (Reactive2P, below). It was mathematically
clean and well understood, but its linear core is the SVF's low-pass and its nonlinearity sat in the wrong place,
so at its chosen operating point it was too close to the SVF. The second prototype (K35) follows the simplified
circuit structure of the early, Korg35-based MS-20 low-pass from Stinchcombe's analysis ([A Study of the Korg MS10
& MS20 Filters](http://www.timstinchcombe.co.uk/synth/MS20_study.pdf), 2006). In a controlled listening comparison
at equal Q and operating point it sounded clearly grittier, and it was chosen.

## Decisions

### Topology: reduced early Korg35

Stinchcombe's figure 3: a Sallen-Key pair with C1 = 3 C2 and R1 = R2 / 3 (the ratio is his inference), whose output
gain stage (a x58 non-inverting amplifier with back-to-back diodes, the "Korg35 chip" role) is in the forward path,
and whose output also drives the resonance through C1. The diodes make that stage a limiter: gain 58 for small
signals, falling to a unity-slope follower offset by a diode drop once they conduct (his section 5). The later
OTA-based MS-20 moves the diodes into the feedback path, so its distortion is concentrated around the cutoff; the
early one distorts across the spectrum, the passband included. K35 targets the early one.

Normalised to unity small-signal passband, with states scaled by the stage gain G, input x = D x_in, loop gain rho:

```text
v   = U1 + rho h(U2)
U1' = w ((x - v) + (U2 - v) / 3)
U2' = w (v - U2)
y   = h(U2)
h(u) = u / G + (1 - 1 / G) K tanh(u / K),   G = 58
```

h has unit slope at zero and slope 1 / G beyond the knee K. Small-signal it is 1 / (p^2 + (7/3 - rho) p + 1),
Stinchcombe's equation 8, self-oscillating from rho = 7/3 (his nominal circuit maximum is about 2.2). The input
enters the poles linearly; the limiter acts on the filtered signal, which is both the output and the resonance
feedback. Not modelled: the signal-dependent, asymmetric cutoff and resonance of the real circuit (his section 4:
the half-cycles ring at about 1.7 and 2 kHz), component tolerances, transistor detail. The asymmetry is a later
voicing experiment, not part of the first product version.

**Solve.** One scalar TPT equation in e = v - U2 (U2 = s2 + g e, no division by g):

```text
R(e)  = A e + (1 + g) s2 - s1 - g x - rho h(s2 + g e) = 0,   A = 1 + 7g/3 + g^2
R'(e) = A - g rho h'(U2) >= 1 + (7/3 - rho) g + g^2 > 0   for rho < 13/3
```

The root is unique and lies in an explicit bracket (the linear part of h moved across, the tanh part bounded).
Safeguarded Newton from the linear solution, as in the SVF and Reactive2P solves.

**Stability below threshold.** In w = dU2, v = dU1 + 4/3 dU2 the incremental dynamics are exactly
w' = w0 ((c - 7/3) w + v), v' = -w0 w with c = rho h'(U2) in [rho / G, rho]: a damped oscillator whose damping
stays positive for rho < 7/3, so forced trajectories converge (the same argument as Reactive2P's). Putting the
limiter on a state inside the loop did not reintroduce the SVF's frequency-division attractors (ADR 0006).

### Voicing (provisional, in Korg35Response.h)

- **Operating point L0 = 0.5:** the knee is 2 in mixer units, so a mixer level of 1 (a unit-peak saw) sits at half
  the knee at Drive 0, and +6 / +12 / +24 dB is L = 1 / 2 / 8. This is part of the sound, not gain staging: Drive 0
  already has grit (a 100 Hz sine at mixer level 1 has about 2 % THD with the cutoff at 3 kHz, 7 % at 300 Hz), and
  Drive makes it progressively nastier. With all three oscillators near full level the stage sees about L = 1.5 at
  Drive 0; that is left as is.
- **Resonance, rho max 2.40:** landmarks Q 0.5 at 0 %, Q 8 at 80 % (about the circuit's nominal maximum), Q 100 at
  95 % and rho 2.40 at 100 %, pinned exactly. Segments are exponential in Q up to 95 %, then linear in rho across the
  threshold, which is crossed at 95.9 %: only the top 4 % of the knob self-oscillates. The joins are rounded by cubic
  Hermite blends (in log 1/Q around 80 %, in rho around 95 %) with harmonic-mean knot slopes, so the map is monotonic
  and C1. Chosen by listening: at 2.2-2.3 the top did not scream enough.
- **Mode: low-pass to the MS-20 high-pass** (revised 2 October 2026; it was low-pass only, with Mode disabled). Mode
  moves the input from the low-pass input to the MS-20's high-pass input; see Mode below. Q Comp does not apply to K35
  and stays disabled.
- **Output stage:** K35's Resonance trim `korg35OutputTrim`, (1 + 4 r)^-0.8 (see Progress), then the voice's
  filter-output DC blocker, 5 Hz first order (see Validation, DC). Until ADR 0008 the blocker was K35's own, before
  the trim; it is now common to all three filters. Both act on the output only; the loop feeds back the limiter
  output h(U2). The trim is numerically the SVF's law but K35 owns it, so SVF voicing changes cannot move K35.

### Rejected: Reactive2P (the first prototype)

Reactive2P was the literal brief: two buffered one-poles (the OTA MS-20's linear structure) with one shared
saturator on the sum of input and resonant feedback,

```text
d = y1 - y2,   q = a tanh((D x + rho d) / a),   y1' = w (q - y1),   y2' = w (y1 - y2),   LP = y2
```

so input and feedback clip in the same element and the input modulates the incremental damping 2 - rho phi' at
audio rate. What it established, and why it lost:

- **Solver and stability: excellent.** Unique scalar solve for rho < 4 (F' >= rho (1 - rho / 4) above threshold);
  1.3-2.2 mean Newton iterations from the linear seed, no fallbacks (the previous-sample seed was worse); exact
  contraction below rho = 2 (a damped oscillator in (dy2, dd)); onset exactly at rho = 2 at every rate; 21 600
  below-threshold periodicity cases all periodic.
- **Input-dependent Q works.** Resonance prominence at rho 1.9 fell from 15.4 dB to 0.7 dB as the normalised input
  rose from 0.03 to 0.48, much steeper than the SVF's. Above threshold a played note entrains or quenches the free
  oscillator; locking was clean (hysteresis mostly within one input-period sample, rational islands narrow and
  weakly held, no wide divisions for symmetric inputs; a 25 % pulse opened a 2:1 region several hundred cents wide
  at rho 3 and above).
- **Structural level coupling.** With one bounded saturator, the input level that quenches the oscillator and the
  oscillator's own amplitude are both set by the knee, so their ratio is fixed. At the clean operating point that
  kept resonance strong at Drive 0 (L0 0.06), self-oscillation sat about 19 dB above the played signal (the shipping
  Ladder sits at about -13 to -14 dB); no output trim can fix a ratio, and the needed gain varied 16-26 dB with
  level.
- **Saturator shape does not help.** Algebraic knees z / (1 + |z / a|^n)^(1/n), n = 2 / 4 / 8, each tuned to the
  same quench point: at most 2.3 dB less self-oscillation (n = 8), bought with a near-instant onset
  (A ~ (rho - 2)^(1/n)) and weaker driven resonance. tanh stayed.
- **Too close to the SVF.** Its linear core is the SVF's low-pass, and its saturator sits before the two poles, so
  distortion is filtered away (at rho 3, 15 % THD inside the loop but 3 % at the output). At L0 0.06 it was a clean
  resonant two-pole that only became interesting when driven: the SVF's territory.

**What the comparison showed.** At the same operating point and Q, K35 and Reactive2P have identical passband
distortion when all harmonics are below the cutoff; above it, K35's output limiter gives about 1.7x the THD (+4.5 dB
of upper harmonics), because they are generated after the poles. The operating point (L0) turned out to be the main
voicing control for grit and for the self-oscillation level; the placement adds the bite. Reactive2P's code and tests
were removed after this record.

## Validation (at the provisional voicing, hidden `[k35-*]` tests unless noted)

- **Linear reference:** matches `LinearTptSvf` with k = 7/3 - rho within 1.4e-12 over all host rates x oversampling;
  the nonlinear filter converges to it at low level (quick suite).
- **Solver:** 50k random problems over g, rho < 13/3, knees and states: at most 6 iterations from the linear seed, 9
  from the bracket ends (quick). Product-like renders at Resonance 95 / 97 / 100 %, Drive 0 to +24 dB, the cutoff
  swept four octaves, then zero input, over the whole rate matrix: 2.6-3.0 mean iterations, worst 5, no fallbacks,
  unconverged or non-finite samples, output peak 2.3 (mixer units).
- **Contraction below threshold:** all tested stimuli converge up to 16x past the knee (quick). Onset at 7/3 at
  every rate (quick).
- **Free oscillation at the top:** at 97 % (rho 2.354) peak 0.19 K, THD 0.4 %; at 100 % (2.40) peak 0.33 K, THD 1.2 %;
  f_osc within 0.03 % of the cutoff; amplitude and frequency drift below 1.3e-7 over 10 s at fc/Fs 0.001-0.05; no
  even harmonics.
- **Locking at 97 % and 100 %:** sine, saw and 25 % pulse at R = 1, 1/2, 1/3 (order 1) and 2 (order 2), Drive 0 to
  +24 dB. From Drive 0 the played note entrains the oscillator across the whole +-300 cent range at R = 1, 1/2, 1/3;
  Drive then turns entrainment into quench. No frequency division: at R = 2 order-2 locks exist only as single points
  with the oscillator unlocked, and louder inputs quench instead. Hysteresis within one input-period sample except one
  42-cent low-side edge (saw, R = 1/3, Drive 0, 100 %).
- **Drive sweep 0 -> +24 -> 0 dB (a slow LFO on Drive):** entrained from Drive 0 (+1 dB at 100 % for a saw), quenched
  from +4 dB (detuned sine) or +9 dB (saw between its 2nd and 3rd harmonics), identical up and down within 1 dB.
- **Aliasing** (inharmonic energy in 20 Hz-20 kHz re total, 48 kHz x 1 / 2 / 4 / 8, a band-limited saw at 90 %
  Resonance): at the 2 kHz cutoff K35 is at the measurement floor (about -88 dB) from 1x at Drive 0 and from 2x even
  at +24 dB; at 8 kHz, Drive 0 needs 2x, +12 dB 4x, +24 dB 8x (-23 to -32 dB at 1x). Comparable to the SVF at its
  maximum Resonance and better at low cutoffs. No in-filter antialiasing is needed.
- **DC:** the limiter partly rectifies inputs without half-wave symmetry (a saw): the output mean is -22 to -47 dB
  re its RMS (the SVF: -39 to -76 dB). The real circuit's output is capacitor-coupled (C19); integration adds a
  DC-blocking high-pass after K35 so the amp envelope cannot turn the offset into thumps (since ADR 0008, the voice's
  filter-output blocker, common to all three filters).
- **Self-oscillation level** (RMS re the played saw through the open filter): at L0 0.5, -1.3 / +4.1 / +7.2 dB for
  rho 2.4 / 2.6 / 3.0; the Ladder is about -13 to -14 dB. With rho capped at 2.40 and entrainment from Drive 0, the
  free oscillator is heard only without input or in its top-of-knob ring.
- **CPU:** see Progress.
- **Product path:** see Progress, Integration (the development override used during prototyping was replaced by the
  `filterK35` parameter).

## Integration

- **Parameters.** `filterType` keeps exactly its two choices (Ladder, SVF), so existing host automation maps as
  before. `filterK35` (bool, default off) is appended after it, the last parameter, so no existing index moves. The
  effective topology is K35 when `filterK35` is on, otherwise `filterType`, which stays underneath (and automatable,
  inaudibly) while K35 is on and is revealed when it turns off. Not an LFO destination.
- **Schema 12** (11 + 1; 12 was never used, the dropped SVF bandpass never reached a schema). Migration of every
  older preset explicitly forces `filterK35` off (any stray entry is replaced), rather than relying on the default.
  Restoring a project saved before K35 existed explicitly resets `filterK35` to off (`setStateInformation`'s
  restore-default list), so a K35-active session never carries K35 into an old project. Factory presets are unedited.
  *1 October 2026:* superseded pre-release. Factory presets are now stored at schema 12, and the preset migrations and
  pre-K35 project reset are removed (no older presets or projects exist).
- **Render path.** The processor branches once per render segment (Ladder batched; SVF or K35 per voice). Each voice
  keeps four K35 lanes beside its ladders and SVFs (and, since ADR 0008, four filter-output DC blockers shared by all
  three types); a switch resets the newly selected filter and uses the existing continuity-offset declick.
- **Editor.** `LADDER | SVF | K35` header tabs. K35 sets `filterK35` and keeps `filterType`; Ladder or SVF turns
  `filterK35` off and sets `filterType`: one undo transaction per click, each write a complete host gesture. Mode and
  Q Comp are disabled (not hidden) for K35; Q Comp's visible text is shortened to "Q Comp" to fit.
- **Development override removed:** Audio Lab uses the product selector; `VektMonoProcessorCost k35` sets `filterK35`.
- **Open:** the user-facing label (internal IDs stay `K35`).

## Final audition

In the real synth, not Audio Lab alone: one saw; three oscillators at substantial levels (the stage then sees about
L = 1.5 at Drive 0); square and pulse; filter-envelope bass; keytracking; Drive 0 / +6 / +12; Resonance around
80 / 90 / 95 / 100 %; switching SVF <-> K35 on the same patch. The question was whether K35 stays distinct from the
SVF in ordinary patches. It passed.

## Deferred

Circuit character left out of the first model on purpose, and engineering left for later. Each is an A/B against the
accepted symmetric K35, kept only if it adds musical character; K35 is "Vekt K35", not a component-level MS-20.

- **Asymmetry** (Stinchcombe section 4): tried and rejected, then revisited at its most favourable settings and closed
  (see Progress): audible in no case.
- **Signal-dependent cutoff and resonance:** the transistor resistors' dependence on signal level.
- **A circuit-specific diode law** in place of the tanh transition of h (optional: h already has the x58-to-unity
  slope that matters).
- **Voicing re-check in normal patches** of L0 = 0.5, rho max 2.40 and the Resonance map.
- **CPU:** done (see Progress, Lane batching).

Timing: K35 has not shipped, so its sound can still change freely. Once it ships, changes to it alter saved patches
and need their own compatibility decision.

## Progress

**Prototype validation (30 September 2026).** Everything under Validation above was measured at the provisional
voicing. The characterizations are hidden Catch2 tests and run in seconds on all cores: `[k35-top-solver]` (6 s),
`[k35-top-drift]`, `[k35-top-locking]` (7 s), `[k35-top-drive-sweep]`, `[k35-aliasing]` (3 s), `[k35-passband]`,
`[k35-self-osc-level]`, and `[k35-renders]` (with `VEKT_MONO_DUMP`, the filter-envelope sweep at Resonance 80 / 95 /
97 / 100 % and Drive 0 / +6 / +12 dB, fixed-gain and RMS-matched, with SVF references). The quick suite (`[k35]`)
covers the linear reference, the solver, contraction, onset, the Resonance map's landmarks, monotonicity and C1
joins, and the development override through the processor.

**CPU (30 September 2026).** `VektMonoProcessorCost` (release, 48 kHz, 256-sample blocks, 16 voices, Resonance 85 %,
Drive +12 dB, single core; `k35` selects the development override), median per block:

| | Ladder | SVF | K35 |
|---|---|---|---|
| 1x | 1357 us | 1311 us | 1509 us |
| 4x | 5451 us | 5160 us | 5756 us |

K35 costs 6-11 % more than the Ladder: scalar lanes and a tanh per Newton iteration (2.6-3.0 per sample). Batching
lanes as the Ladder does is the obvious optimisation, left until after integration.

**Level (30 September 2026, before the trim).** In the fixed-gain renders (an A2 saw at mixer level 1, filter-envelope sweep) K35 is
about -10.5 dBFS at Drive 0 across Resonance 80-100 %, against -19.8 dBFS for the SVF at 100 % with its trim: about
9 dB louder, and nearly independent of Resonance because the played note entrains the resonance. Switching gain is
calibrated during integration.

**Reactive2P retired (30 September 2026).** Its header, tests and Audio Lab entry were removed after the record
above. The development override now offers only K35 (`DevelopmentFilter::korg35`).

**Integration (30 September 2026).** Everything under Integration above is in place.

- **Switching level** (`[mono-switch-gain-k35]`, hidden; one voice, the ADR 0006 measurement saw, notes 36 / 48,
  cutoffs 300 Hz / 1.2 kHz / 5 kHz). Untrimmed, K35 matched both filters at Resonance 0 but rose with Resonance
  (K35 - SVF +7.8 / +9.8 / +11.1 dB at 50 / 80 / 100 %): the Ladder loses passband with Resonance and the SVF trims
  for it, K35's passband does not fall. No constant trim fits (it would leave about 10 dB at one end). The SVF's own
  one-dimensional trim fits almost exactly, so K35 uses the same law, as its own `korg35OutputTrim(Resonance)`. Result, K35 minus SVF / minus
  Ladder, RMS dB: Drive 0: -0.7 to +0.9 / means 0 to +1.8 (worst -2.0 and +4.2 at 100 %); +12 dB: -1.1 to -5.1 /
  -1.3 to +5.9; +24 dB: -0.8 to -4.7 / -7.2 to +10.0 (characterized, as for the SVF). Pinned at Drive 0 by
  `[switch-gain]`: within 1.5 dB of the SVF, within 3 dB of the Ladder below full Resonance, under 6 dB everywhere.
  *Revised 2 October 2026:* the 1.5 dB bound to the SVF applies below full Resonance; at 100 % it is 3 dB. The SVF's
  maximum Q rose from 8 to 20 (ADR 0006), and at 100 % note 48's 9th harmonic sits near the 1.2 kHz cutoff, so the SVF
  is now 2.5 dB louder there (K35 - SVF -1.4 / -2.5 dB at notes 36 / 48). At full Resonance the two filters differ by
  design; matching them would undo the SVF's extension.
- **DC.** At the output stage (the core driven by a band-limited saw, whole periods): -36 dB re RMS before the
  blocker, numerically zero after it; the blocker is -3 dB at 5 Hz and within 0.3 dB from 20 Hz. Found on the way:
  the shipping Ladder and SVF carry more DC than K35 does in a driven patch (about -18 and -23 to -29 dB re RMS at
  +24 dB); Mono has no DC blocking after them. That is outside this ADR. *Clarified by ADR 0008:* those figures were
  taken with the processor's start-up sound, the first factory preset (unison 2x, three detuned oscillators, delayed
  vibrato), and part of what they show is slow beating: the window means moved by as much as the mean at +24 dB.
  Controlled single-saw measurements then established a genuine steady offset (plain saw through the processor:
  Ladder -13 dB, SVF -43 dB at +24 dB; the Ladder down to -9 dB in its worst case), from the filters' saturation. ADR
  0008 removes DC at one common per-layer filter-output boundary before the amp envelope, K35's blocker included.
- **Ladder and SVF bit-identical:** the `[mono-dump]` fixtures, now with SVF variants (8 Ladder, 5 SVF renders over
  44.1 / 48 / 96 kHz, 1x / 2x / 8x and Multicore), match a clean build of the previous commit byte for byte.
- **Regression tests** (quick suite): `filterK35` appended, default off, and `filterType`'s mapping unchanged;
  schema 11 presets (with or without a stray K35 entry) migrate to 12 with K35 off and their Ladder/SVF kept while K35
  is active; a pre-K35 project restores its own Ladder/SVF while K35 is active, and a K35 project restores K35;
  `filterType` automated under K35 changes nothing audible and is revealed exactly when K35 turns off; the K35 switch
  declicks both ways over either filter; the DC blocker as above; finite and bounded under hostile modulation
  (Resonance 100 %, +24 dB, LFO on cutoff and Drive, unison, noise) at 1x and 8x, with and without Multicore; the
  Resonance landmarks, rho max 2.40 and the 95.9 % crossing; the editor's tabs, one-step undo, disabled Mode and
  Q Comp, and header layout at every size.

**Accepted (30 September 2026).** The real-synth audition (see Final audition) passed. The symmetric reduced model
is the baseline for the Deferred experiments.

**Asymmetry experiment, first round (30 September 2026).** Development only: `Korg35Asymmetry` in the core (off by
default: then bit-identical to the accepted K35, checked sample for sample against it), an Audio Lab
`Asym: Off | Limiter | Dynamic` selector and amount, never in parameters, presets or state (checked: Ladder and SVF
render bit-identically with it on, and Mono's saved state is unchanged). One mechanism at a time.

- *Limiter:* the diode knee is K (1 + lambda) for positive and K (1 - lambda) for negative inputs, lambda = 0.5 x
  amount. Unit slope at 0 and 0 < h' <= 1 keep the solve and its uniqueness (tested: bracket and convergence over
  random problems).
- *Dynamic:* one sample late, s = tanh(y[n-1] / K) moves this sample's cutoff by exp(-alpha s) (+-8 % at full
  amount) and loop gain by (1 - beta s) (+-2 % of rho): a positive half-cycle lowers the pole frequency and weakens
  the resonance. Stinchcombe's simulation has the lower-ringing half resonating less (positive: about 1.7 kHz and
  weaker; negative: about 2 kHz and stronger); which polarity of the normalised output corresponds to his is an
  assumption and only mirrors the effect.

Measured (`[k35-asymmetry-table]`, `[k35-asymmetry-top]`, hidden; amounts 25 / 50 / 100 %):

- *Ring asymmetry* (50 Hz square into K35 at 2 kHz; ring frequency and decay after each edge; Q 8 and Q 43).
  Dynamic produces the designed effect, modestly: f+/f- 0.98 / 0.96 / 0.93, the positive half decaying 1-5 % faster.
  The limiter is not the harmonics-only stand-in it was meant to be: K35's limiter is inside the resonance loop, so an
  unequal knee makes the half-cycles ring differently too, and far more strongly: f+/f- 1.04 / 1.08 / 1.38 with the
  positive half decaying 1.4 / 2 / 4-5x slower (the opposite direction to the circuit).
- *DC* (raw, before the output blocker): the limiter adds a lot (-24 dB re RMS at Drive 0, -11 dB at +12 dB at full
  amount; off -42 / -37); dynamic barely changes it. The blocker removes it either way.
- *Free oscillation* at 97 / 100 %: no drift (below 3e-8 over 10 s) in any variant; even harmonics 0.8-2.8 % (limiter)
  and 0.8-1.4 % (dynamic) at full amount; the limiter lowers the amplitude (0.23 K against 0.33 K at 100 %).
- *Solver* over the rate matrix at full amount: no non-finite, unconverged or fallback steps, worst 5 iterations;
  output peak 3.2 (limiter) and 2.3 (dynamic, as off).
- *Locking* at 97 / 100 %, full amount: 1:1 and R = 1/2 unchanged (entrained across +-300 cents, no hysteresis or
  islands). Breaking the symmetry opens 2:1 locking, as expected, but narrowly: order-2 tongues up to about 80 cents
  wide in both modes (off: single points), all quenched at +24 dB; hysteresis mostly within one sample, at most
  about 54 cents (limiter, pulse). Far from the wide division regions rejected earlier (hundreds of cents).
- *Aliasing* (8 kHz, note 1760 Hz, 90 %): dynamic as off; the limiter is 8-18 dB worse at 4x and 8x at +24 dB
  (-54 / -74 dB against -63 / -89).

Both variants are stable and bounded. The decision is by A/B in Audio Lab; if Dynamic wins, the next step is its
current-sample (implicit) version and a check whether removing the one-sample lag is audible.

**Asymmetry, second round (30 September 2026).** Limiter kept only as a comparison control: it acts as a different
nonlinear filter (wrong direction, too strong, large DC, worse aliasing), not an approximation of the circuit's
asymmetry; unless it wins the A/B decisively it is removed. Dynamic's amount now extends to 200 % for the A/B: cutoff
depth keeps scaling (+-16 % at 200 %), the rho shift is capped at +-3 % (reached at 150 %).

- *Ring asymmetry* at Q 8, low cutoff (the model's own value): f+/f- 0.926 / 0.893 / 0.878 / 0.864 at 100 / 150 /
  175 / 200 %; the positive half decays 1-8 % faster.
- *The one-sample lag shapes it:* f+/f- depends only on cutoff / Fs (8 kHz at 96 kHz reads as 4 kHz at 48 kHz) and
  weakens towards high cutoff: at 175 %, 0.877 at 500 Hz and 0.898 at 8 kHz / 48 kHz (about 17 % of the effect lost
  there), 0.881 at 8 kHz / 192 kHz. The current-sample (implicit) version would remove this dependence.
- *At 200 %:* solver clean over the rate matrix; free oscillation drift-free, even harmonics up to 2.7 %; 1:1 and
  R = 1/2 locking unchanged; aliasing as off. The order-2 (2:1) tongues grow with the amount: 27-72 cents at Drive 0,
  72-198 cents at +12 dB (80 cents at most at 100 %), all quenched at +24 dB, hysteresis at most 35 cents. The A/B
  should listen for divide-down (a note an octave above the resonance at +6 / +12 dB) as the cost of more amount.

**Asymmetry rejected (30 September 2026).** In the Audio Lab A/B, Dynamic (up to 200 %: f+/f- 0.86, the range the
circuit reference suggests) was indistinguishable from Off. By the round's own criterion (an effect that cannot be
heard does not earn a more complex model, let alone the implicit same-sample solve) the refinement is rejected. The
Limiter variant was only a comparison control: it behaves as a different nonlinear filter (wrong direction, strong
level dependence, large DC, worse aliasing) rather than the circuit's mechanism, so it goes too. All experiment code
(the core's asymmetry settings, the voice and processor plumbing, the Audio Lab controls and their tests) was removed;
the core and the tests are byte-identical to the accepted K35 again. The measurements above stay as the record.

**Lane batching (30 September 2026).** The solve is now a per-lane Newton state machine (`NonlinearTptKorg35Newton`),
advanced one tanh at a time, so the scalar path and batched lanes run the same safeguarded logic. `processLanes`
groups lanes four, then two, at a time with a shared vector tanh (`SimdLanes.h`, Apple simd, about 2 ulp from libm,
as the Ladder's batched solve); the processor batches every sounding voice's layers in a render unit into one solve,
as for the Ladder. Two savings apply to the scalar path too: the output reuses the tanh of the solve's last
evaluation (the same argument, so bit-exact), and a voice's layers share one tan. The scalar path is bit-identical to
the accepted K35 (checked sample for sample, iteration counts included); batched lanes stay within 2e-14 relative of
it below threshold (`Mono K35 batched lanes match the scalar solve`). Ladder and SVF stay byte-identical.

`VektMonoProcessorCost` (release, 48 kHz, 256-sample blocks, 16 voices, Resonance 85 %, Drive +12 dB), median per
block:

| | Ladder | SVF | K35 (before) |
|---|---|---|---|
| unison 1, 1x | 1390 us | 1342 us | 1413 us (1566) |
| unison 1, 4x | 5627 us | 5270 us | 5458 us (6064) |
| unison 4, 1x | 4458 us | 3961 us | 4285 us |
| unison 4, 4x | 17878 us | 15225 us | 16259 us |

K35 is now level with the Ladder at unison 1 and cheaper with unison.

**Asymmetry closed (1 October 2026).** Revisited in case the first A/B's settings (saw, sweeps, the self-oscillating
top) had hidden it. Dynamic (previous-sample, as before) was re-rendered outside the filter at the settings most
favourable to it: low square and 25 % pulse notes (41-55 Hz, alternating edges), a fixed cutoff well above them (2 and
3 kHz), Resonance 88-95 % (long rings, below self-oscillation), Drive 0 / +6, with a saw as negative control and an
offset-matched symmetric control. Every sample differs, but little: the level-matched difference from Off is -34 to
-36 dB for the squares, -31 dB for the saw and -27 dB for the pulse at 200 % (-40 / -37 / -33 dB at 100 %), concentrated
in the same frequencies as the resonant ringing that masks it. In a blind A/B (dynamic 200 % against the control) the
pairs were indistinguishable. The mechanism is real but inaudible in this filter: closed, not to be revisited. (The
revisit's temporary render test was removed with it.)

**Mode (2 October 2026).** K35's Mode now feeds the MS-20's high-pass input: `highPass` b = (Mode + 1) / 2 splits the
driven input into x_lp = (1 − b) x at the low-pass input and x_hp = b x at the lifted ground end of C2, which is how the
MS-20 builds its high-pass from the same Korg35 circuit (Stinchcombe, section 7: grounding the LP input and driving C2
gives a 6 dB/oct high-pass, not 12). With C2's state W = U2 − x_hp the per-sample equation is the low-pass solve with
s2 + x_hp in place of s2, so the residual, its unique root, the bracket and the batched lanes are unchanged.
Small-signal, `y = [x_lp + (p² + 4/3 p) x_hp] / (p² + (7/3 − ρ) p + 1)`: Mode +1 is a 6 dB/oct high-pass with the
low-pass's poles (same resonance and self-oscillation threshold); Mode 0 is the full signal at −6 dB with a resonant
bell at the cutoff (flat at ρ = 1, a shallow dip below it). The MS-20 itself never blends the two inputs: its HPF and
LPF are separate circuits in series, each with its own cutoff, which one Cutoff cannot reproduce.
- *Candidates, auditioned in Audio Lab and offline:* this input blend; a crossfade LP → series HP→LP band-pass → HP;
  and HP→LP in series with Mode spreading their cutoffs (the MS-20's two cutoff knobs folded into one). Thomas chose the
  blend by listening. The audition selector, its header and its render test were removed.
- *Low-pass unchanged:* at b = 0 the arithmetic is the previous K35's; a scratch dump of scalar and 1–5 batched lanes
  (shared, own and mixed-rate settings, hostile controls) at -O0 and -O3 was bit-identical, and the `mono/k35`
  reference render passes.
- *Tests:* `[k35-highpass]` checks the small-signal high-pass (6 dB/oct below the cutoff, unity above) and the half
  blend's flat −6 dB at ρ = 1; the hostile-solver, contraction and batched-lane tests now cover b = 0.5 and 1; K35's
  Mode sweep is click-free; hostile modulation sweeps Mode by LFO.
- *Level, characterised:* at Mode +1, Drive 0 (1.2 kHz, notes 36 / 48, RMS) K35's high-pass is +0.2 to +4.6 dB above
  the SVF's, since its 6 dB/oct slope keeps more of a note's low harmonics, and 6.6 to 16 dB above the Ladder's, whose
  high-pass is far quieter at high Resonance (the SVF's is 2.5 to 13 dB above it too).
- *Level, matched (later the same day):* under Mono's level-matching policy (ADR 0005, Notch → HP level) K35's
  high-pass side gets a constant −3 dB (`korg35HighPassTrim`), eased in across Mode 0 → +1; the low-pass and the bell
  are unchanged. K35 − SVF at HP, K-weighted: +2.4 / +4.0 / +2.1 dB before, −0.6 / +1.0 / −0.9 after (worst −4.4) at
  Resonance 0 / 50 / 90 %. The bell (Mode 0) varies with whether a harmonic sits on the cutoff (−5.6 to +9.3 dB) and
  is characterised, not bounded.
- *High-pass input level, considered and rejected (2 October 2026):* at high Resonance K35's high-pass carries a large
  non-linear residue (−13.6 dB at 50 %, −3 to −6 dB at 90 % re its linear output on a detuned three-oscillator mix;
  −29 dB at 0 %): the resonance drives the diode stage, which the low-pass's note body masks. Lowering the HP-side
  input (with make-up after the filter, as the Ladder's HP then did; ADR 0009 later replaced it with a high-pass ladder
  that needs no input law) cleans it by 10–30 dB, but the diode limiting is
  also what holds K35's high-Q resonance: with less of it the HP's resonance came up by as much as +7 to +11 dB at
  90–93 %, and the fade needed before self-oscillation made the level dip by as much again toward 95.9 %. Auditioned
  at 0 / −6 / −12 / −18 dB in Audio Lab; Thomas kept it off (more problems than it solves). The limited, gritty HP is
  K35's character. Measurement kept: `[mono-hp-distortion]`.

