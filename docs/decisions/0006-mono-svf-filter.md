# ADR 0006: Mono SVF Filter

> Kobber was called **Mono** until 3 October 2026. Dated entries and records below keep the names of their time:
> Mono, `vekt::mono`, `plugins/vekt_mono`, `Mono*` types, `[mono]` and `[mono-*]` tags, test names "Mono ...", tools
> `VektMono*` (now `VektKobber*`), `VEKT_MONO_*` variables (now `VEKT_KOBBER_*`), and the Audio Lab tools
> `VektRavAudioLab` and `VektRavRender` (now `VektAudioLab` and `VektRender`).

## Status

Proposed. The SVF's nonlinear architecture is accepted (amplitude-increasing
damping, below), its voicing is frozen, and the Filter Type UI is in place.

Architecture decisions below are settled for prototyping. The nonlinear notch
behaviour under Drive, the Drive/knee calibration and the resonance curve
between its endpoints are left for the prototype to answer (see Open questions).

**Progress (29 September 2026):** Steps 1–3 are done. `LinearTptSvf` is the
linear reference. `filterType` (schema 10) selects it per render segment; each
voice keeps four SVF lanes alongside its ladders. Provisionally, the SVF path
maps Resonance linearly from `k = 2` to `k = 0.05` and outputs LP, without Drive
or Mode, until step 4. There is no Filter Type UI yet. With Filter Type = Ladder,
renders were bit-identical to the previous build on the development fixtures
(the hidden `[mono-dump]` test) at 1x/2x/8x, 44.1/48/96 kHz and with Multicore.

**Progress (29 September 2026, step 4, linear):** The SVF path now mixes
LP → Notch → HP from its native outputs (`svfModeMix`), using the same smoothstep
easing as the ladder (`filterModeEase`, now shared). It reads the same smoothed,
LFO-modulated Mode value. The landmarks are bit-exact; the sweep is monotonic,
continuous and flat at −1/0/+1. The low passband holds unity from LP to Notch
and the high passband from Notch to HP, and the level at the cutoff is
`|w_HP − w_LP| / k`, reaching the notch at 0. Resonance stays on the provisional
linear-in-`k` map (`svfDamping`). Ladder renders remain bit-identical on the
dump fixtures.

**Progress (29 September 2026, step 5; first topology, superseded the same day by
amplitude-increasing damping, see Open questions):** `NonlinearTptSvf` replaces the linear
reference on the render path. It solves `F(h)` by safeguarded scalar Newton,
seeded with the linear solution, inside the tighter analytic bracket
`x - k s1 - s2 ± g a (k + 1)`. A step is bisected when it leaves the bracket, or
when it is more than half the step before last (rtsafe) *and* the bracket has
not halved in two iterations. Plain rtsafe was not enough: plain Newton
2-cycles on the S-shaped `F` of a saturated chain, and when `phi` saturates
exactly, `F` is exactly linear with its root on the bracket end, where rtsafe's
test sits on its equality boundary. The state update is `s <- 2y - s`, once per
sample. Heard HP is `phi(h)`. The knee is provisionally `a = 1`. Findings:

- *Solver:* over 200k random problems across the whole range, the linear seed
  needs 2 iterations or fewer in 84 % of cases (at most 14); adversarial edge
  seeds need at most 17. In the render path (16 voices, Resonance 85 %,
  +12 dB), it averages 2.7 iterations, at most 3, with no fallbacks. It
  converges to `LinearTptSvf` within 1e-5 relative at 1e-5 input over host rate
  × oversampling × cutoff × damping. Zero-input decay holds at maximum
  Resonance and Drive, with the cutoff modulated, over all 16 host × OS rates.
- *CPU:* the whole processor with 16 voices is about 20 % slower on SVF than on
  Ladder (release, 48 kHz: 1648 vs 1353 µs median at 1x). The SVF recomputes
  `tan` per lane and uses scalar libm tanh, while the ladder batches four lanes
  with vectorised tanh. Optimisation is step 9.
- *HP leak:* at 50 Hz, heard HP equals raw HP within 0.1 dB up to +18 dB. The
  bass there is correct HP response, not leakage. Leakage appears only when the
  LP integrator slew-limits (a = 1, amplitude 1.5, +24 dB: −14 dB heard vs
  −12 dB raw), and `phi(h)` bounds it at `±a` rather than cancelling it.
- *Notch:* shallow already at Drive 0: −13 dB at a = 1, −30 dB at a = 4
  (amplitude 0.5, k = 0.5). At the cutoff `h ~ x / k`, so it is compressed.
  The saturated-input reference stays exact.
- *Resonance:* strongly compressed. At k = 0.05, the LP gain at the cutoff is
  +4 dB at a = 1 and +13 dB at a = 4, against +26 dB linear, because the
  saturated integrators cap the LP amplitude there at about `a`. The heard HP
  is capped at `±a` too.
- *Subharmonic locking:* at Q of about 12.5 and above, a driven sine near the
  cutoff can lock the filter into a large f/3 subharmonic. Essentially all the
  output power moves to f/3, with a peak above the input's. It stops with the
  input (no self-oscillation), but it is very audible. It was not seen at
  Q 8.3 or below in a coarse sine scan, at either knee.

The knee, Drive calibration and resonance cap are now one coupled voicing
decision; see Open questions. `[svf-characterize]` (hidden) prints the table.

**Voicing candidate (29 September 2026):** knee `a = 4` and
`k: 2 → 0.125` (Q 0.5 → 8), still linear in `k`. At Drive 0, the LP passband
THD is ≤ 0.3 % at mixer levels up to 1.5. The notch at the cutoff is −30 dB at
level 0.5 with Q 2, −19 dB at level 1, and about −9 dB at Q 8 (`h ~ x/k`
saturates). The Q 8 LP peak is +8 to +13 dB against +18 dB linear. Zero-input
decay still holds across the rate matrix.

**Period-multiplication scan (29 September 2026):** the fine scan covered
Q 6–12 × knee 3/4/5 × Drive 0–24 dB × sine/saw/square × input at
0.5/1/2 × fc × cutoff 200/1k/5k × level 0.5/1. It used exactly periodic inputs
and measured `rms(y[n+N] − y[n]) / rms(y)`. It found persistent period-2
attractors (`y[n+N] ≈ −y[n]`) at every Q and knee. A Q cap does not remove
them: a saw at the cutoff at +18 dB period-doubles already at Q 2 (71 % of the
power in the f/2 family) and at Q 4 (99 %). At Q 8 this begins around +12 dB.
The behaviour persists unchanged from 48 kHz to 768 kHz, so it belongs to the
continuous-time model, not the discretisation. It sits in a Drive window
(present at +18 dB, absent at +12 and +24 dB for that case).

- The cause is saturating the first integrator, `phi(h)`. With `phi` on the
  second integrator only, the scan is exactly periodic at Q 2 and Q 4. It
  still fails from Q 6 at knee 4 and from Q 5 at knee 3, but not up to Q 7 at
  knee 5, so the boundary is not monotonic in the knee.
- The Ladder, over the same grid, is exactly periodic up to Resonance 95 %,
  including at +24 dB. Each ladder stage integrates `tanh(in) − tanh(out)`,
  which contracts. The SVF's saturated drive into a bare integrator inside a
  global loop does not.

The acceptance criterion, no persistent subharmonic attractor in the normal
range, is therefore not met by this nonlinearity placement at any usable Q with
+12 to +18 dB Drive. See Open questions.

## Context

Mono has one filter: the four-stage nonlinear TPT ladder of ADR 0005, with an
LP → Notch → HP pole mix on its output. A 12 dB mode derived from the ladder
was considered and rejected: it would inherit the ladder's four-stage feedback
dynamics, resonance and saturation, and so be the same filter with a different
slope. Mono is no longer constrained to an expanded Minimoog; a second,
genuinely different topology is worth more than another response from the
ladder.

The second filter is a two-pole nonlinear TPT state-variable filter in the
OTA/SEM family. It is inspired by that lineage, not a model of any unit.

## Decisions

### Product

- **Filter Type** is a discrete `LADDER | SVF` switch (secondary text
  `4-POLE | 2-POLE`), not a slope control. It is not a continuous morph and not
  an LFO destination. The panel title "Ladder Filter" becomes "Filter".
- Cutoff, Resonance, Drive, Mode and Env Amt are shared controls whose meaning
  is topology-appropriate, not numerically matched.
- **True cutoff semantics.** Cutoff is the characteristic frequency of the
  selected topology. Vekt applies no hidden topology-dependent frequency offset.
  At equal Cutoff and low Resonance the SVF sounds substantially brighter than
  the ladder; that is the intended consequence of choosing a two-pole filter,
  not a compatibility defect. Envelope, LFO, keytracking and automation move the
  same physical frequency under either type.
- **Resonance.** Each topology has its own mapping from the shared control.
  Ladder: unchanged, self-oscillates near maximum (ADR 0005). SVF: 100 % is very
  strong but decaying resonance, a finite distance from `k = 0`.
- **Drive** is one concept: input gain `D = 10^(driveDb / 20)` over the same
  0 to +24 dB range, into a fixed nonlinear knee. The knee is not a second
  Drive-dependent control (input gain and knee scale are equivalent up to output
  gain). +12 dB means "substantially driven" in both filters, not identical THD.
- **Q Comp** remains Ladder-only. Under SVF it is shown disabled, not hidden, so
  the panel geometry does not move. (The Ladder's **Saturated Taps** A/B was
  retired in sound schema 11: the saturated taps are its only Notch/HP mix.)
- **Bandpass** was tried and dropped (30 September 2026). The prototype was an
  SVF-only switch that turned Mode into LP → BP → HP, with BP taken from the
  damping term `k psi(B)` (exactly unity at the cutoff at any Drive) and no
  Resonance trim. Untrimmed, BP sat within 0.3 dB of the trimmed LP/HP at
  50 % Resonance and 8 dB below at 100 %; the full trim would have put it
  19 dB below. It worked technically: no click on engaging, and no change at
  the LP/HP landmarks. By listening, though, the LP → Notch → HP sweep was
  preferred. A 2-pole BP is broad (6 dB/oct skirts), and its blends with LP
  and HP sound like weaker versions of them. If BP is revisited, try a
  constant-skirt (SEM-style) BP, whose peak rises with Q, rather than the
  constant-peak one.

### DSP

Let `x` be the driven input `clamp(D x_in, -24, 24)` (the ladder's bound),
`g = tan(pi fc / fs)` at the effective (oversampled) rate, `k` the damping
(`k = 1/Q`), `s1`, `s2` the trapezoidal integrator states and `phi` a monotonic,
bounded, tanh-like saturator with `|phi| <= a` and `phi' >= 0`.

**Linear reference** (the canonical TPT SVF):

```text
HP = (x - (k + g) s1 - s2) / (1 + k g + g^2)
BP = s1 + g HP        s1 <- BP + g HP
LP = s2 + g BP        s2 <- LP + g BP
Notch = LP + HP = x - k BP
```

**Nonlinear first topology (historical).** Superseded on 29 September 2026 by
amplitude-increasing damping (Open questions; `NonlinearTptSvf.h`), whose heard
outputs are its states and whose notch cancels the fundamental at the cutoff at
every level. The φ(h) notch behaviour described here no longer applies. The
integrator drives saturate; the damping feedback stays linear. With `h = HP_raw`:

```text
BP(h) = s1 + g phi(h)
LP(h) = s2 + g phi(BP(h))
F(h)  = h - x + k BP(h) + LP(h) = 0
F'(h) = 1 + g phi'(h) (k + g phi'(BP)) >= 1
```

For `g > 0` and `k >= 0`, `F` is strictly increasing, so each sample has
exactly one root. It lies in the explicit bracket `[x - B, x + B]` with
`B = k (|s1| + g a) + |s2| + g a`. The production solver is safeguarded scalar
Newton within that bracket, with residual/iteration diagnostics in the style
of the ladder's. The heard HP is `phi(h)`, the quantity the integrator chain
actually receives, and Notch is `LP + phi(h)`. This keeps heavy Drive from
passing the driven input through HP/Notch unfiltered, the failure the ladder's
saturated taps address. At small signals `phi(h) ~ h` and the response is the
linear SVF; under Drive the notch depth, frequency and symmetry become
amplitude dependent by design.

A one-pass linearised solver is not planned. It is revisited only if profiling
shows a real CPU problem, and then must match static sweeps, resonance tuning,
driven spectra, modulation behaviour and listening.

**Measurement references** (test harness only, never shipping candidates):

- *Raw HP:* hear `HP_raw`; quantifies the heavy-Drive feedthrough failure.
- *Saturated input + linear SVF:* `phi(x)` into the linear reference; the exact
  notch of the signal entering the filter, for attributing notch changes in the
  production candidate.

**Mode** maps `-1 LP → 0 Notch → +1 HP` using the same smoothstep easing as
`ladderPoleMixTaps`, so the knob feels the same under both types.

**Render path.** Filter Type is global to a render segment. The processor
branches once per segment between the batched ladder solve and a batched SVF
solve; there is no per-sample virtual dispatch. With Filter Type = Ladder the
output is bit-identical to the ladder path before this change.

**Switching.** A type change triggers the voice's existing continuity-offset
declick. The new filter starts from zero state and its natural state
acquisition (ring-up time about `Q / (pi fc)`, ~80 ms at 80 Hz, Q 20) is
allowed to be audible; a short crossfade would not hide it. The inactive
topology is not run. A `Q/fc`-scaled crossfade is investigated only if
listening in the hostile cases (low cutoff, high resonance, sustained bass)
finds a real problem.

**State.** (*1 October 2026:* preset migrations removed pre-release; factory presets are stored at the current
schema.) `filterType` is added in sound schema 10; older presets migrate to
Ladder.

### Gain policy

- *Nominal:* at Resonance 0 and Drive 0 both topologies have unity passband
  gain (the ladder's `1/(1+k)` is 1 at `k = 0`; the SVF LP is unity at DC).
- *Switching target:* steady-state level change on switching of about 3 dB or
  less on the representative fixtures.
- *Hard ceiling:* under 6 dB across the specified Resonance × Drive matrix.
  Meeting the ceiling but not the target is not "good".
- Measure fundamental/passband gain as well as broadband RMS: a saw going from
  24 dB to 12 dB legitimately gets brighter, which is not a gain failure.
- If high Resonance breaks the ceiling, add a topology-specific
  resonance/output calibration only large enough for sane gain staging. The
  two filters' loudness behaviour is not forced to match.

## Validation

Solver correctness and dynamical policy are separate tests.

- **Linear reference:** LP/BP/HP/Notch match the analytic prewarped responses;
  `|LP(fc)| = |HP(fc)| = |BP(fc)| = 1/k`; deep notch at `fc`; DC and Nyquist
  limits; bounded output under audio-rate cutoff modulation.
- **Solver:** root within bracket, residual and iteration bounds, no
  non-finite output, over the full parameter range.
- **No self-oscillation:** excite, set input to zero, max Resonance, max Drive,
  cutoff modulated by LFO and drift: energy decays to silence, decay slope stays
  negative, no finite-amplitude limit cycle, no net growth from modulation.
- **Rate matrix:** host rate {44.1, 48, 96, 192} kHz × oversampling {1, 2, 4, 8},
  i.e. effective rates up to about 1.536 MHz, including the very-low-cutoff /
  highest-rate corner where `g` is smallest.
- **Heavy Drive:** HP bass leakage and Notch/HP bound at +12/+24 dB for the
  production candidate against both references.
- **Ladder:** bit-identical render with Filter Type = Ladder.
- **Switching:** no click on type change in the hostile cases; ring-up judged
  by listening.
- **Gain:** the target and ceiling above on the fixtures; documented
  Ladder-vs-SVF listening on identical oscillator fixtures.

## Implementation order

1. This ADR.
2. Linear TPT SVF reference with transfer-function, tuning and notch tests.
3. `filterType`, schema 10 migration, render-path branch; Ladder bit-identical.
4. Mode and the independent SVF resonance mapping.
5. Nonlinear integrator model (safeguarded scalar Newton), the reference A/B,
   and heavy-Drive leakage and nonlinear-notch tests.
6. Nominal gain calibration, then switching level at high Resonance/Drive.
7. Continuity-offset switching; crossfade only if needed.
8. Ladder-vs-SVF listening fixtures.
9. Solver optimisation only if profiling requires it.
10. Bandpass and any SVF-specific resonance compensation.

## Open questions

- Nonlinear notch behaviour under Drive: how far `LP + phi(h)` departs from the
  reference notch at +12/+24 dB, and whether that sounds right.
- Knee `a` and Drive calibration for the SVF. At `a = 1` the SVF is well into
  saturation at Drive 0 on normal mixer levels: shallow notch, capped HP and
  a heavily compressed resonance. A knee a few times the mixer level keeps
  Drive 0 near-linear and lets Drive reach saturation.
- Resonance cap versus subharmonic locking: `k_min = 0.05` (Q 20) permits f/3
  locking under drive; the coarse scan puts the boundary between Q 8 and 12.
  The fine scan supersedes this: period-2 attractors appear from Q 2 under
  +18 dB Drive (see the period-multiplication scan). The options are (a) limit
  the SVF's Drive range, (b) accept period doubling as heavy-Drive character,
  (c) move the saturation to the second integrator (clean to Q 4 at knee 4),
  or (d) a nonlinearity placement that is contractive by construction, e.g.
  saturating storage (`BP' = w (x − k phi(BP) − phi(LP))`, `LP' = w phi(BP)`,
  whose energy `Phi(BP) + Phi(LP)` only falls without input). That needs
  prototyping and the same scan.
- Saturating storage (tried 29 September 2026, now in the tree):
  `B' = w (phi(x) − k phi(B) − phi(L))`, `L' = w phi(B)`, heard
  `LP = phi(L)`, `BP = phi(B)`, `HP = phi(x) − k phi(B) − phi(L)`. It keeps
  the scalar solve (`F(B)`, `F' >= 1`, bracket
  `s1 + g (phi(x) ± a (k + 1))`) and is dissipative without input. It is
  scale-equivariant below the input clamp, so the knee only rescales the input
  level. Scanned in normalised level (`D A / a`, 1/24-decade steps, 1 s and 2 s
  windows so transients do not count), it has persistent attractors in narrow
  level windows from Q 2.5: 2–7 of 576 cases per Q between Q 2.5 and 8, at levels
  about 0.5–1.4, from a saw whose 2nd or 3rd harmonic sits on the cutoff. At
  a = 4 and mixer level 1 that is Drive of about +6 to +15 dB. Q 2 is clean.
  The first topology, on the same scan, fails 25–100 of 576 cases per Q from
  Q 2, over levels up to 4.5. HP bass rejection is ideal (heard HP stays on the
  linear response through +18 dB). But the notch only holds while
  `|x| < k a`: −24 dB at level 0.5 and Q 2, nearly gone at Q 8. LP THD at
  Drive 0 is 0.5 % at level 1. Zero-input decay and low-level equivalence
  pass. The hidden `[svf-periodicity]` test is the level-swept regression.
- **Amplitude-increasing damping (29 September 2026, the current candidate):**
  `u = a tanh(D x / a)`, `B' = w (u − k psi(B) − L)`, `L' = w B`, with
  `psi(B) = B + beta B^3 / a^2` and `psi' >= 1`. The B–L coupling is linear
  and skew, so two trajectories under the same input lose the energy of their
  difference at `w k dB (psi(B1) − psi(B2)) >= w k dB^2`: damping only grows
  with amplitude, and states are driven together. It is not a proof for the
  discretised, time-varying filter; the scan remains the gate. Trapezoidal
  states give `F(B) = (1 + g^2) B + g k psi(B) − C`, with
  `C = s1 + g u − g s2` and `F' = 1 + g^2 + g k psi' > 0`. The root lies
  between 0 and `C / (1 + g^2)`, and the seed is `C / (1 + g^2 + g k)`. The
  heard outputs are the states: `LP = L`, `BP = B`,
  `HP = u − k psi(B) − L`.
  - *Periodicity:* on the level-swept scan (48 kHz, Q 2–12,
    beta 0/0.25/0.5/1, levels 0.05–16), 0 of 17 280 cases were persistent,
    and the largest non-periodicity was about 2e-14. The same detector flags
    25–100 cases per Q on the first topology.
  - *Notch:* the fundamental at the cutoff cancels exactly at every level and
    Drive. The notch is `B'/w0 + L`, and for any periodic B its fundamental is
    `(j − j) B1 = 0` at w0, so it can only fill through harmonics.
  - *HP bass:* follows the linear response until u saturates, then stays
    bounded. No leak.
  - *LP:* set by the input saturation alone. THD at level 1 is 0.6 % at
    Drive 0, then 2 %, 7 % and 18 % at +6, +12 and +18 dB.
  - *Resonance at Q 8 (linear +18 dB), level 1:* at Drive 0, +17.9 / +15.1 /
    +13.9 / +12.6 dB for beta 0 / 0.25 / 0.5 / 1; at +24 dB, +32.0 / +22.4 /
    +20.6 / +18.9 dB. beta = 0.5 is provisional.
  - *Solver and CPU:* at most 4 iterations; F is convex on the root's side,
    so Newton never leaves the bracket and the fallback is unreachable in
    practice. In release with 16 voices it is slightly cheaper than the Ladder
    (1308 vs 1392 µs median at 1x, 5105 vs 5584 µs at 4x).

**Architecture accepted (29 September 2026).** The work moves to voicing:
beta, then the resonance curve up to Q 8, then Ladder↔SVF switching gain, then
the Filter Type UI. Q 8 stays as the maximum for instrument reasons, not
stability: the model is periodic to Q 12, but more Q would compete with the
Ladder's self-oscillating range instead of differing from it. (Revised
2 October 2026: Q max 20; see Maximum Q.) The Newton
fallback stays as insurance. The periodicity sentinel runs in the quick
suite; the full sweep is hidden (`[svf-periodicity-full]`, about 4 min).

**Resonance prominence (29 September 2026):** this is the LP fundamental at
the cutoff over the LP passband fundamental at the same Drive (level 1,
`[svf-prominence]`, hidden). The passband tracks the saturated drive u within
0.1 dB throughout, so this ratio isolates the damping law. At Q 8 (linear
18.1 dB):

| beta | Drive 0 | +6 | +12 | +18 | +24 dB |
|---|---|---|---|---|---|
| 0 | 18.0 | 18.0 | 18.0 | 18.0 | 18.0 |
| 0.25 | 15.2 | 12.8 | 10.4 | 8.9 | 8.4 |
| 0.5 | 14.0 | 11.3 | 8.8 | 7.2 | 6.7 |
| 1 | 12.6 | 9.7 | 7.0 | 5.4 | 4.9 |

At Resonance 90 % (Q 3.2, linear 10.1 dB), beta 0.5 gives 8.7 → 3.2 dB over
the same Drive range. Resonance THD stays under 4 %, while the passband THD
from the input saturation reaches 33 % at +24 dB. beta moves overall level by
only about 0.5 dB on the audition fixture.

**Switching level, first look:** on the audition fixture (one voice, A2 saw,
envelope sweep, Resonance 100 %), the SVF is 12.4 / 15.8 / 12.6 dB louder in
RMS than the Ladder at Drive 0 / +12 / +24 dB. That is well over the 6 dB
ceiling. The Ladder's passband falls as `1/(1+k)` at high Resonance, and
the SVF's does not. The correction belongs on the SVF output, so Ladder
presets are unchanged; it is calibrated after beta and the resonance curve.

**Cutoff range (29 September 2026):** at the old 20 Hz minimum, a fully
closed SVF still passed bass notes (about −14 dB re open for a 55 Hz
note, −40 to −46 dB at 110–220 Hz), while the Ladder was effectively silent
(−40 to −80 dB). That follows from the 12 dB/oct slope under true cutoff
semantics. So Cutoff now reaches 5 Hz, and the internal floors (voice, both
SVFs, Ladder) moved from 10 Hz to 2.5 Hz, keeping one octave for modulation.
Closed, the SVF is now −35 to −42 dB at 55 Hz and −60 to −74 dB at
110–220 Hz. Presets store Cutoff in Hz and keep their sound; the knob's travel
shifts, so existing host automation of Cutoff maps to slightly different
frequencies. Nothing changes above 10 Hz, and the Ladder dump fixtures are
bit-identical.

**beta locked at 0.5 (29 September 2026):** chosen by listening in Audio Lab.
0.25, 0.5 and 1 sound alike in ordinary playing. The damping only engages
when the band state is large relative to the knee (high Resonance, high
Drive, cutoff on a strong harmonic), and the audible step is between having
the damping at all (beta 0) and not.

**Resonance shape (29 September 2026, in audition):** `k = 2 − 1.875 r^p`
keeps both ends (Q 0.5 and Q 8); a lower p brings resonance in earlier.
Candidates in Audio Lab (development override; the built-in shape is p = 1):

| Resonance | p = 1 | p = 0.7 | p = 0.5 | p = 0.35 |
|---|---|---|---|---|
| 25 % | Q 0.65 | Q 0.78 | Q 0.94 | Q 1.18 |
| 50 % | 0.94 | 1.18 | 1.48 | 1.89 |
| 75 % | 1.68 | 2.14 | 2.66 | 3.28 |
| 90 % | 3.2 | 3.87 | 4.52 | 5.18 |

**Voicing frozen (29 September 2026):** beta 0.5, p 0.5, Q max 8, knee
a = 3, and SVF output trim `(1 + 4 r)^-0.8`, all in `SvfResponse.h`. p was
chosen by listening in Audio Lab. The knee was chosen on switching level,
since 3 and 4 sound alike: knee 3 is knee 4 with 2.5 dB more Drive and 2.5 dB
less output. At mixer level 1 knee 3 gives about 1 % LP THD at Drive 0 (4:
0.6 %, 2: about 2 %). Knee 2 was not chosen for switching gain alone: it moves
the whole SVF 6 dB deeper into saturation. The Audio Lab audition overrides
for beta, p and knee have been removed.

**Maximum Q (2 October 2026):** Q max rises from 8 to 20 (`svfMaximumQ`); the
SVF still cannot self-oscillate. `svfDamping` keeps the 29 September law
`k = 2 − 1.875 √r` exactly up to Resonance 90 % (Q 4.5), then lowers k by a
smoothstep from 0.125 to 1/20 over the top 10 % (Q about 7.4 at 95 %, 13 at
98 %), monotonic with no slope step at the join. Reasons: the Q 8 cap was an
instrument choice, not a stability limit, and K35 has since joined the Ladder
in self-oscillating at the top of its knob. The SEM, the SVF's model family,
also has almost no damping at small signals at full resonance; its diode
network adds damping as the band-pass level grows, as `psi(B)` does here, so
Q mostly lengthens the ring of quiet signals and of harmonics on the cutoff.
- *Periodicity:* the full sweep (`[svf-periodicity-full]`, now Q 2/4/8/12/16/20,
  48 and 192 kHz) has 0 of 8,640 cases persistent.
- *Audition* (`[svf-q-renders]`, hidden; Q 8/12/16/20 at Resonance 95/100 %,
  Drive 0/+12/+24 dB, sweeps and plucks, against the Ladder): differences are
  modest. Q 8 → 20 at 100 %, Drive 0: sweep RMS +1.9 dB and peak +3.8 dB; a
  pluck with the cutoff on a harmonic +4.2 dB RMS, a filter-envelope pluck
  +1.7 dB, between harmonics +0.9 dB. Thomas chose Q 20 by listening for the
  wider range.
- *Prominence at 100 %* (`[svf-prominence]`, beta 0.5, level 1): 16.7 dB at
  Drive 0 falling to 9.7 dB at +24 dB, against about 14 → 7 dB at Q 8.
- *Switching level at Drive 0:* SVF − Ladder stays within 2.2 dB at 100 %.
  K35 − SVF is −1.4 / −2.5 dB there (notes 36 / 48; note 48's 9th harmonic sits
  near the 1.2 kHz cutoff), so ADR 0007's 1.5 dB bound now applies below full
  Resonance and is 3 dB at 100 %.
- *Decay:* the zero-input decay tests pass at Q 20; at the 2.5 Hz floor the
  tail's time constant is about 2.5 s, so that test runs 7.5 s.

**Switching gain (29 September 2026):** the trim follows 80 % of the Ladder's
`1/(1+4r)` passband loss in dB, leaving the SVF slightly more open. Measured
SVF minus Ladder RMS on a controlled saw (cutoff 300/1.2k/5k, notes 36/48,
knee 3): Drive 0, −1.9 to +4.7 dB (97 % within 3 dB); +12 dB, −0.2 to
+6.7 dB; +24 dB, −4.4 to +10.8 dB. The requirement is revised: at Drive 0 the
gap must be under 6 dB, and within 3 dB for nearly all normal cases (enforced
by `[switch-gain]`). Through +12 dB the target is under 6 dB. Above +12 dB it
is characterised and pathological jumps are avoided, with no ceiling. At
+24 dB the two filters are deliberately different nonlinear systems, and the
residual is two-signed across cutoff and Resonance. No Resonance × Drive ×
cutoff gain table: the remaining discrepancy is documented as a consequence
of the topologies. Per Mode at Drive 0: LP and Notch are within 3 dB. HP in
its passband is +0.5 to +5.5 dB: the 20 % the trim leaves, plus the slopes
just above the cutoff. HP with the cutoff above the note is +6 to +12 dB RMS,
which is stopband content through the 12 dB/oct slope, the same 2-pole
character as a closed LP. The trim stays independent of Mode.

**Filter Type UI (29 September 2026):** the panel is titled "Filter", with
`LADDER | SVF` header tabs styled like the LFO tabs (tooltips give 4-pole /
2-pole). They are bound to `filterType` through a `ParameterAttachment`, so
presets, automation and undo show on the tabs. Q Comp is disabled, not hidden,
under SVF. The shared filter-knob tooltips describe both
types.
- The SVF resonance curve between its endpoints: `k = 2` (Q 0.5) or
  `k = sqrt(2)` (Butterworth) at 0 %, and `k_min` (provisionally about 0.05,
  Q 20) at 100 % with its margin from `k = 0`. Linear interpolation in `k` spends
  most of the control on low Q; exponential in Q (`Q = 0.5 * 40^r`) is more even:

  | Resonance | 25 % | 50 % | 75 % | 90 % | 100 % |
  |---|---|---|---|---|---|
  | Q, linear in k | 0.66 | 0.98 | 1.86 | 4.1 | 20 |
  | Q, exponential in Q | 1.26 | 3.2 | 7.9 | 13.8 | 20 |

  Choose after the nonlinear SVF exists, since saturation changes how high Q feels.
