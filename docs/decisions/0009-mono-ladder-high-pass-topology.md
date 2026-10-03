# ADR 0009: Mono Ladder High-Pass Topology

> Kobber was called **Mono** until 3 October 2026. Dated entries and records below keep the names of their time
> (Mono, `vekt::mono`, `plugins/vekt_mono`, `[mono]`, test names "Mono ...").

## Status

Accepted (2 October 2026) by Thomas's Audio Lab audition (Phase 3 of the high-pass ladder topology track,
`docs/KOBBER_LADDER_ACCEPTANCE_PLAN.md`). Implemented in `NonlinearTptLadderHighPass.h` and `MonoVoice`
(`finishLadderSample`). A listen to the shipping build is open. Supersedes the high-pass layers of the interim baseline
(`40bb819`; ADR 0005, "Notch → HP level" and "HP input level and top").

## Context

The Ladder's Mode mixed the low-pass ladder's taps from LP through Notch to HP (ADR 0005, `LadderPoleMix.h`). The tap
high-pass, `r (1 − H)^4 / (1 + k H^4)`, had a resonant peak (1 + k) times weaker than a real high-pass ladder's, so it sat
up to 13 dB under the SVF and K35; and its saturating first stage turned a hot, detuned mix into jittery, static-like
intermodulation that only the high-pass exposed. The interim baseline compensated with a level lift, an 18–30 dB input
reduction with make-up, a Drive rule, a "stop short of self-oscillation" compression and state rescaling. Each layer was
measured and auditioned, but together they were a calibration layer around a linear tap mix on a non-linear ladder, and
at +24 dB Drive the lift over-corrected by 9–10 dB. Thomas chose to replace the high-pass with a true high-pass ladder.

## Decision

1. **Topology (placement F).** Four linear trapezoidal one-pole high-pass stages under global feedback; the input
   saturates as `3 tanh(D x / 3)` (the SVF's knee) and only the feedback saturates, `u_1 = v − k tanh(y_4)`. Each sample
   is the scalar `F(u_1) = u_1 + k tanh(A u_1 + B) − v` with `F' ≥ 1`: a unique root in `[v − k, v + k]`, safeguarded
   Newton with one `tanh` per step. Small-signal it is the low-pass ladder's mirror, `HP^4 / (1 + k HP^4)`.
2. **Mode (M1).** Up to Notch the voice keeps the tap mix exactly. Across Notch → HP the low-pass ladder solves at
   Notch (with the raw Resonance) and the output crossfades by `filterModeEase(Mode)` into the high-pass ladder. The
   high-pass ladder runs whenever Mode is above LP, so it is settled before the crossfade reaches it. It rests (reset,
   no cost) only where most patches sit: no LFO on Mode and Mode at LP for 1 s, so a knob or automation move through LP
   does not restart it. It restarts primed with the steady state for the current input (`prime`). A restart still rings
   against the warm filter it would have been, about as loudly as the signal on a square Mode LFO even when primed, so
   modulated Mode never rests it, however long it dwells at LP.
3. **Top of Resonance.** The high-pass ladder stops just short of self-oscillation: `ladderHighPassFeedback` uses the
   low-pass ladder's law up to 90 % and compresses the top so 100 % is 97.9 % (k = 3.916). LP and Notch keep the
   low-pass ladder's self-oscillation; across Notch → HP it fades out with the crossfade (as auditioned in M1).
4. **No level lift, input law, Drive rule or state rescaling.** The high-pass ladder's high band loses 1 / (1 + k) with
   Resonance like the low-pass ladder's bass, which the SVF's and K35's Resonance trims already follow.

## Alternatives considered

- **Placement A (saturating stages, `y = u − LP_tanh(u)`).** Matched the level without a lift and aliased much less
  than the tap high-pass, but without input management its level jittered 1.4–6.5 dB on a detuned mix (the linear beating
  is 0.02–0.67 dB); it needed the baseline's input law, oscillated 5–10 % flat and cost 1.7–2.6× the low-pass ladder.
- **M2** (the low-pass ladder crossfading into the high-pass ladder over the whole range): loses the Notch. **M3** (a
  snap at Notch): a level step of about 20 dB at Notch. Chosen by ear: M1.
- **Self-oscillating high-pass at the top:** about 11 dB louder at 100 % with 9.9 dB level swings while a note plays;
  Thomas preferred stopping short.
- **Keeping the baseline layers** (or a Drive-dependent fade of the lift): an empirical correction on a correction.

## Evidence (2 October 2026)

Filter level (48 kHz; `[ladder-hp]`, hidden `[ladder-hp-prototype]`, `[ladder-hp-aliasing]`):
- Linear response exactly the bilinear `HP^4 / (1 + k HP^4)` over 44.1–192 kHz × 1/8 and k 0 / 2 / 3.6.
- On a Classic Three Bass-like mix without any input management: non-linear residue −20 to −21 dB and 50 ms level range
  0.8–1.6 dB, the same as the SVF's high-pass (−21 dB, 1.0–1.6 dB).
- Self-oscillation (where allowed) exactly at the cutoff (0.9992–0.9999 fc); decays to silence below threshold.
- Aliasing at 1x: −45 to −69 dB inharmonic (tap high-pass −29 to −47, SVF −47 to −63).
- Drive behaves like the SVF's: residue −6 dB at +12 dB, −1 dB at +24 dB.

Through the processor (Classic Three Bass, cutoff 1 kHz unless noted):
- Switching level, K-weighted, Ladder − SVF at HP, Resonance 0 / 50 / 90 %: −1.6 / −1.0 / +1.9 dB at Drive 0,
  −1.5 / −0.1 / +0.7 at +12 dB, −1.5 / +0.7 / +0.8 at +24 dB, with no lift (`[switch-gain]` and
  `[mono-switch-gain-modes]`); the baseline's +9 to +10 dB at +24 dB is gone.
- Level against Resonance is smooth at every Mode (`[mono-ladder-resonance-level]`).
- Overshoot over the louder of before and settled (`[mono-ladder-mode-transient]`, Resonance 100 %): a Mode jump LP →
  HP +2.1 to +4.5 dB, the high-pass ladder's own resonant build-up (the SVF's: −0.2 dB, its low- and high-pass share one
  state); a Resonance jump 80 → 100 % at HP +2.1 / +4.5 dB at 250 Hz / 1 kHz (the SVF's: +4.1); a full-depth Mode LFO
  crest 7–10 dB (the SVF's: 8). Unchanged in a temporary build that ran the high-pass ladder always (not kept), so the
  Mode-jump overshoot is not a restart artefact: Mode is smoothed over 20 ms, and the filter runs before it is heard.
- Restart: on a 3 Hz square Mode LFO through the startup preset, resetting the high-pass ladder at LP rang against a
  run that never reached LP: loudest 5 ms −10 to +5 dB re the signal at cutoffs 100 / 250 / 1,000 Hz, Resonance 90 /
  100 %, primed or not. Kept running, it is −15 to −16 dB, the two runs' Mode-trajectory difference, the same as in the
  always-running build (`Mono Ladder high-pass keeps running under Mode modulation`; `Mono Ladder high-pass rests only at
  unmodulated LP` checks a 0.25 Hz square LFO never rests it). Priming a restart on a saw, against the warm filter, at
  Resonance 0: −37 to −53 dB from a 1 kHz cutoff, −30 / −15 dB at 250 Hz (55 / 220 Hz saw), unprimed −12 to −17 dB;
  with Resonance the ring remains (`… primes a restart on a running signal`, values with `-s`). Primed on a constant
  input it is exactly settled, up to Drive +24 dB and the top feedback (`… primes to the exact steady state`).
- 50 ms level range at HP: 2–5 dB at 1 kHz, Resonance 90–100 % (the resonance lifts a partial near the cutoff of this
  drifting, detuned patch); 1.7–3.1 dB at 3 kHz (`[mono-hp-jitter]`).
- Release cost, the acceptance plan's Step 5 workload (`VektMonoProcessorCost {44100|48000} {128|257} 8 1 30
  mode={1|-1}`, 8 voices, 1x, unison 1, 30 s, default scheduler; simulated callbacks, not device deadlines): at Mode +1
  the first runs met the measured timing rule (p99.9 under 75 % of the deadline, no simulated exceedance) in 3 of 4:
  p99.9 0.64 / 0.64 / 1.19 ms at 44.1/128, 48/128, 48/257 against 75 % bounds of 2.18 / 2.00 / 4.02 ms. 44.1/257
  failed on one 7.7 ms callback (median 0.98 ms, p99.9 2.62 ms; cause unproven); two repeats met it (p99.9 1.16 /
  1.15 ms). Under the plan's evidence policy Step 5 stays unqualified for Mode +1. Median against Mode −1: +45 %
  (0.49–1.00 ms against 0.34–0.69 ms). Mode −1 met all four (p99.9 0.47–0.85 ms) and is unchanged: the high-pass ladder
  rests after the first second.
- Beyond Step 5, unison 4 (`… unison=4`): Mode −1 meets the rule in all four (p99.9 1.23–2.42 ms). Mode +1 meets it at
  257 samples (p99.9 3.54 / 3.61 ms) but is marginal at 128: 44.1 kHz p99.9 1.85 ms against 2.18 with 3 simulated
  exceedances (fail); 48 kHz 2.05 against 2.00 with 9 (fail), a repeat 1.85 with none (pass). With `multicore`, median
  0.28 ms; 48 kHz/128 met the rule, 44.1 kHz/128 had one exceedance (3.06 ms).

## Consequences

- The Ladder's Mode above LP, or with an LFO on Mode, costs about 45 % more CPU (median, 8 voices, unison 1; the
  high-pass ladder runs scalar on every layer, with the coefficients shared, and the low-pass ladder at Notch), and so
  does the first second at LP after a voice or filter reset; unison 4 at 128-sample blocks is marginal on one core. Vectorising the high-pass ladder across lanes, as the low-pass ladder's batched
  solve does, is the obvious follow-up.
- `ladderPoleMix` keeps its own Notch → HP half for the solver's API, tools and tests; the voice no longer uses it.
- Q Comp acts on the low-pass ladder only, so it fades out of the sound across Notch → HP.
- The high-pass side no longer self-oscillates (ADR 0005's oscillation requirement holds for LP and Notch).
- Presets with the Ladder above Notch change sound (Quartet Pad's LFO takes Mode to about +0.4). Mono has not shipped.
