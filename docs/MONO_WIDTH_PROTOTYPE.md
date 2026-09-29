# General Width prototype (29 September 2026)

The existing `osc*PulseWidth` host IDs and 5–95% range are unchanged. The host
parameter is displayed as **Width** and now warps phase for all four Morph anchors.
At 50%, the previous waveform renderer is used without modification. At other
settings, the square is a fixed 50% waveform whose edge is moved solely by the
shared phase mapping; it does not apply the legacy pulse-width comparison again.

The experimental `MonoVoiceSettings::widthDcPolicy` selects `raw` (the default,
retaining the old static pulse DC) or `zeroCentered` for *internal A/B tests*.
It is intentionally not a host parameter or saved in presets. Zero-centering
subtracts each anchor's frozen-width analytical mean before Morph, with no RMS
normalization. The policy must be chosen before shipping or migrating presets.

The off-center renderer places value corrections at the saw/square wrap and
square edge, and slope corrections at the phase-warp breakpoint, wrap and
triangle corners. These local BLEP/BLAMP corrections are a **prototype**, not
proven antialiasing for fast Width modulation or overlapping events. In the
48 kHz / 3517.3 Hz diagnostic at 5% Width, the corrected square measures
-0.98 dB nonharmonic energy relative to total, compared with -1.14 dB for
the uncorrected warp (worse). At 20% Width, triangle is also slightly worse
(-9.30 vs -9.49 dB). Do not treat the passing finiteness/continuity tests as
alias acceptance. Measure moving Width and Morph, pitch, Quality and full
ladder output, and improve event handling before shipping the feature.
An independent offline Fourier reference now lives in
`tools/audio_lab/MonoWidthReference.h`. It midpoint-samples the **ideal**
warped anchor shapes (without calling the production renderer), FFTs a dense
cycle, keeps only harmonics below host Nyquist, and synthesizes a static output
with complex harmonics. It preserves the existing Morph weights, reports DC,
RMS and fundamental magnitude/phase, and supports both DC policies. A 65,536
versus 131,072 point convergence check covers the narrow 5% square's retained
harmonics; float FFT precision and finite sampling remain reference limits.
At 48 kHz and 3517.3 Hz, direct sample errors against this reference expose
cases where the current candidate is *worse* than naive (e.g. the 5% sine,
-16.19 versus -20.67 dBFS); they are diagnostics, not an acceptance gate.
The bandlimited periodic reference intentionally does not model moving Width,
the nonlinear ladder, or host oversampling/decimation.

The test grid also sweeps Morph and frozen Width for finite fundamental
magnitude, checks per-anchor frozen-width DC, and exercises abrupt Width/Morph
changes through a driven voice; none of those checks establishes click-free
modulation. The previously failing resonance fixture disabled only oscillator 1,
while the factory preset left oscillators 2 and 3 enabled (including a non-neutral
Width); the fixture now disables all three, as its noise-only description intended,
and passes without changing the resonance threshold.

Existing non-neutral pulse patches, including morph blends, can sound different
because of the new antialias corrections and width affecting other anchors.
The neutral-width path is preserved; factory presets have not been changed.

## Static renderer comparison against the Fourier reference

`tests/processor/MonoWidthRendererComparisonTests.cpp` renders the ideal
bandlimited reference, raw phase warp and existing corrected oscillator at the
same internal rate, then downsamples each with the same `OversamplingBank` path
used by Mono: 1x off, 2x IIR, 4x FIR, 8x FIR. A settled 2048-sample host window
at 48 kHz uses coherent bin 37 (867 Hz) or 151 (3539 Hz), Width 5/20/50/80/95%,
and Morph 0/1/1.5/2/2.5/3: **60 points per quality**. The reference retains
only harmonics strictly below host Nyquist. A scoring self-test distinguishes
a phase-shifted fundamental from a nonharmonic spur and DC.

The comparison measures complex error at every in-band harmonic (thus phase
*and* magnitude), host-rate nonharmonic/alias power, DC and RMS differences,
and time-domain error. All powers below are relative to the reference signal
power, in dBc; less-negative means worse. Worst cases are not necessarily the
same grid point. No numeric acceptance limits have been approved.

| Quality | Corrected has lower alias power | Worst raw alias | Worst corrected alias | Worst raw harmonic error | Worst corrected harmonic error |
|---|---:|---:|---:|---:|---:|
| 1x | 56/60 | -9.77 | -15.79 | -44.03 | -13.68 |
| 2x IIR | 58/60 | -16.73 | -35.15 | -50.05 | -25.24 |
| 4x FIR | 58/60 | -22.40 | -30.85 | -56.06 | -37.15 |
| 8x FIR | 56/60 | -27.09 | -30.50 | -62.28 | -49.16 |

The corrected renderer generally suppresses aliases more effectively, while
raw is consistently closer to the ideal in-band harmonic amplitudes/phases at
the worst point. For example, at 3539 Hz / 5% / sine, raw vs corrected harmonic
error at 1x is -105.3 vs -13.7 dBc; the alias power is -15.9 vs -15.8 dBc.
At 8x, raw aliases still reach -27.09 dBc over the grid, so oversampling alone
does **not** establish that the raw warp is production-ready at the extremes.
Nor does lower alias power establish that the current corrections sound right.

These are *oscillator-only* static tests: no filter, drive, LFO movement, note
envelope, stereo processor output, or performance measurement. The comparison
uses the actual downsampler, but supplies an internally generated oscillator
instead of running through the full voice/processor. Exact neutral-width output
is preserved in production; continuity of the renderer at 49-51% is still open.

## Event-geometry iteration (static Width only)

`MonoVoice.h` now describes the off-center waveform using bounded, stack-local
`WidthEvent { phase, valueJump, slopeJump }` entries. The sine, triangle, saw
and square events are derived from their actual left/right values and
`dW/dphi = W'(psi) * dpsi/dphi`; the triangle corners are at `d/2` and
`(1+d)/2`. Finite-difference tests of the *independent ideal waveform* confirm
all event jumps at 5/10/20/49/51/80/95%. This is a representation/refactor,
**not an antialias improvement**: applying the same short polyBLEP/polyBLAMP
kernels to the same events produced unchanged grid scores from the table above.

A diagnostic blend `raw + alpha * (corrected - raw)`, for alpha
`0/.25/.5/.75/1`, shows no single global correction-strength fix. At 1x, full
strength has the lowest alias power in 56/60 points but raw has the lowest
complex harmonic error in all 60; at 8x these counts are 56/60 and 41/60.
The worst corrected harmonic errors at 1x for alpha `0/.25/.5/.75/1` are
`-44.03/-25.72/-19.70/-16.18/-13.68 dBc`; strongest alias suppression
generally requires the full correction. No alpha is exposed to production.

An additional 3539 Hz static probe includes 10%, 49%, 49.99%, 50%, 50.01%,
51% and the extremes at 1/2/4/8x. At 5% sine / 1x, raw versus corrected
complex harmonic error is -105.3 versus -13.7 dBc, while alias power is
-15.9 versus -15.8 dBc; 10% sine / 1x has corrected harmonic error -23.0
dBc. The exact 50% path remains untouched. Across 49.99/50/50.01%, these
coherent static scores do **not** establish continuity under Width modulation;
a dedicated moving-width test is still needed. The event-list rewrite does not
justify shipping this renderer. Investigate kernel accuracy or longer residuals
before moving to Width modulation, DC listening or preset changes.

## Offline long-residual experiment (not production)

`tools/audio_lab/MonoWidthLongResidual.h` implements a zero-phase, fractionally
indexed, Blackman-windowed sinc impulse integrated into a step residual and a
ramp residual (256 subdivisions/internal sample). It applies the verified value
and phase-derivative jumps, including events in adjacent cycles if the support
overlaps another period. It renders **all widths including 50% through one
algorithm**. The support radii 8/16/32/64 are *on each side* of an event, i.e.
16/32/64/128 internal samples total. This is an offline upper-bound test, not a
causal realtime renderer. The production oscillator and its neutral branch are
unchanged.

The same coherent, settled 48 kHz comparison, downsamplers and host-Nyquist
Fourier reference cover 96 points per quality: bins 37/151, widths
5/10/20/49.9/50/50.1/80/95%, and Morph 0/1/1.5/2/2.5/3. With Nyquist-centered
sinc (cutoff 1.0 relative to internal Nyquist), results are:

| Quality | Radius | Worst alias dBc | Worst complex harmonic error dBc | Better than short correction on both | Lower alias than raw |
|---|---:|---:|---:|---:|---:|
| 1x | 8 / 16 / 32 / 64 | -15.62 / -15.41 / -15.13 / -14.91 | -32.31 / -38.76 / -43.33 / -56.98 | 92 / 94 / 94 / 94 of 96 | 92/96 each |
| 2x | 8 / 16 / 32 / 64 | -33.38 throughout | -65.22 / -79.56 / -79.58 / -79.58 | 9/96 each | 94/96 each |
| 4x | 8 / 16 / 32 / 64 | -30.38 throughout | -73.11 / -79.67 / -79.68 / -79.68 | 4/96 each | 94/96 each |
| 8x | 8 / 16 / 32 / 64 | -30.38 throughout | -79.59 / -79.67 / -79.67 / -79.67 | 6 / 5 / 5 / 5 of 96 | 94/96 each |

The long renderer beats the short correction on *complex harmonic error in all
96 points per quality*. But at 2x/4x/8x it seldom also beats the short
correction's alias power, and its worst alias power remains roughly -30 dBc
at 4x/8x. At 1x/3539 Hz/5%/sine, radius 32 yields alias -15.13 vs short
-15.79 dBc, despite harmonic error -93.78 vs -13.68 dBc. At the same pitch,
5%/square yields alias -35.89 vs -24.68 dBc and harmonic error -92.62 vs
-20.81 dBc. Therefore **a longer Nyquist-centered kernel resolves much of
the in-band error but does not demonstrate a simultaneous worst-case alias
improvement**. Radius 64 has diminishing returns at oversampled rates.

At radius 32, lowering the sinc cutoff from 1.0 to 0.85 changes the 1x
both-metrics win count from 94/96 to 59/96 (harmonic wins 96 to 61), with
no increase in alias wins (94/96); at 4x the both-metrics count stays 4/96.
That one cutoff probe is not a general filter-design optimization. The
reference only retains *host*-Nyquist harmonics, while the residual filters
at the *internal* Nyquist and uses the production decimator; their transition
bands and folded products are not identical.

At 49.9/50/50.1% the long renderer's frozen-width adjacent output differences
are under 0.01 RMS in the dedicated two-pitch, 1x/4x, four-anchor test;
frozen-width DC and RMS errors are under 0.001. At **exactly** 50%, the long
renderer is not sample-identical to the legacy renderer: at 1x/3539 Hz the
long-minus-legacy RMS is ~0 for sine, 0.0167 for triangle, 0.0691 for saw,
and 0.0522 for square. This static check does not establish click-free Width
modulation or support replacing the legacy branch. No moving-width, causal
latency, performance, preset, ladder, or DC-policy decision follows from it.

**Decision:** this offline construction does not satisfy the proposed BLEP
stop condition across all qualities; do not port it into the voice as-is.
Next controlled architecture experiment is a Fourier-generated bandlimited
Width/pitch table using the existing reference, with separate modulation
sideband validation. Do not interpret the alias score as an exact measure of
all folded energy: coherent aliases landing on harmonic bins appear in the
complex-harmonic-error metric instead.

## Offline Fourier-table architecture experiment (static Width only)

`tools/audio_lab/MonoWidthTablePrototype.h` builds four-anchor, Width-frame,
two-band float tables from independently generated complex coefficients in
`MonoWidthReference.h`. Each level truncates the *same* coefficient series at
the highest pitch it serves, respecting both the internal and the stricter
host-rate Nyquist in this test, and inverse-FFTs a phase-aligned cycle. The
test bands have ceilings at the coherent 48 kHz bins 37 (867.19 Hz; 27
harmonics) and 151 (3539.06 Hz; 6 harmonics). The lookup uses the existing
Morph weights, either linear or four-point Lagrange phase interpolation, and
linear or four-point Width interpolation. Width frame spacing is either uniform
in percentage or uniform in logit(width/100). All Widths, including 50%, use
one lookup path. An on-grid coefficient-reconstruction check catches FFT
scaling, phase and harmonic-limit errors. This is **not** used by MonoVoice.

For a 17/33-slice sweep at 1x and 4x, two coherent pitches and 11 Widths
(including 49.9/50/50.1), every anchor and two Morph blends (264 cases per
configuration), worst nonharmonic power is approximately -135 dBc at 2048
phase samples with either phase interpolator, versus approximately -76 dBc
at 1024 samples with *linear* phase lookup (-125 dBc with cubic). Beyond
2048 samples, improving phase lookup does not materially reduce the *worst
complex harmonic error*, which is dominated by Width-frame interpolation:

| Width frames | Spacing | Worst harmonic error, linear Width lookup | Max fundamental phase error | Raw table data (two bands, 2048 samples) |
|---:|---|---:|---:|---:|
| 17 | uniform | -14.17 dBc | 0.00987 rad | 1,114,112 bytes |
| 17 | logit | -11.94 dBc | 0.00843 rad | 1,114,112 bytes |
| 33 | uniform | -20.24 dBc | 0.00417 rad | 2,162,688 bytes |
| 33 | logit | -15.73 dBc | 0.00293 rad | 2,162,688 bytes |

With 2048 phase samples, cubic *phase* lookup and a separate 168-point
4x Width-density sweep (including 7/15/90/93%) show:

| Width frames | Spacing | Width lookup | Worst harmonic error | Max abs RMS error | Max fundamental phase error |
|---:|---|---|---:|---:|---:|
| 33 | uniform | linear / cubic | -19.43 / -19.69 dBc | 0.00723 / 0.00320 | 0.00720 / 0.0000735 rad |
| 33 | logit | linear / cubic | -15.83 / -15.85 dBc | 0.01027 / 0.00449 | 0.00293 / 0.0000376 rad |
| 65 | uniform | linear / cubic | -29.71 / -35.42 dBc | 0.00275 / 0.000827 | 0.00210 / 0.0000413 rad |
| 65 | logit | linear / cubic | -25.47 / -27.33 dBc | 0.00399 / 0.00155 | 0.000393 / 0.0000329 rad |

Worst nonharmonic power on that 4x sweep is about -135.5 dBc for every
variant; the 65-uniform/cubic worst harmonic case is bin 37, 90% Width,
square (Morph 3). The 65-slice, 2048-sample, two-band bank uses 4,259,840
bytes of *sample data*; vector metadata/allocations and any real pitch-band
coverage are extra. More Width slices and cubic Width lookup substantially
improve fidelity, but **even this candidate does not yet converge closely
enough to the Fourier oracle in worst-case in-band harmonics**. Uniform
spacing outperformed this particular logit spacing on the measured harmonic
grid; the phase-error ranking alone does not overturn that finding.

At 49.9/50/50.1%, the selected 65-uniform/cubic bank passes diagnostic
limits of -110 dBc nonharmonic power, -40 dBc complex harmonic error,
0.0001 absolute DC error and 0.001 absolute RMS error over bins 37/151 at
1x/4x and six Morph positions. Frozen-width 49.9-to-50% or 50-to-50.1%
output differences reach 0.00777 RMS (bin 37, 1x, square); this compares
*different ideal waveforms*, not renderer error. The 50% table output is
not sample-identical to the existing neutral oscillator: table-minus-legacy
RMS reaches 0.07153 at bin 151, 1x, saw. No compatibility choice is made.

**Limits and next question:** The bank has only two pitch ceilings, no
crossfade or validated behavior between/across levels, no Width or Morph
modulation test, no real-time CPU/cache measurement, and no guarantee for
pitches above its highest ceiling. Its unusually low nonharmonic score is
specific to coherent, static test tones and host-Nyquist-truncated tables;
it does not establish click-free pitch-band transitions or modulation safety.
Before production integration, reduce the remaining Width-interpolation
harmonic error (especially the high-Width square) and measure safe pitch-band
switching. DC policy, presets and ladder remain out of scope.

## Mirror symmetry and greedy Width knots (offline follow-up)

For the canonical phase-warp shapes, away from ideal jump locations,
`W(phase, 100-width) = -W(1-phase, width)`; the raw and analytical
zero-centered policies both preserve this identity. The **bandlimited**
coefficient relation is `c[n](100-width) = -conj(c[n](width))`, including
the DC term. At widths 5/7/10/20/35/49.9/50% and both test pitch bands,
the maximum measured reference coefficient discrepancy is 3.46e-8.
Only storing the 5–50% frames of a 65-uniform bank reduces its two-band
2048-sample float data from 4,259,840 to 2,162,688 bytes (65 to 33 frames).
This is an offline `mirrorUpper` option, **not** a production change. The
first half-bank implementation reflected the query *before* interpolation,
which changed the cubic stencil near 50% and gave a worst sampled difference
of 0.02073 from the full bank. The revised version exposes virtual upper-half
frames to the **same** Width stencil: a requested upper frame samples its
stored mirrored lower frame at reversed phase and inverted sign. The worst
full-versus-half sample difference on the symmetry probe is now 1.34e-7;
across a separate 15-Width, two-pitch, six-Morph 4x grid it stays below
4.2e-7 for 4/6/8-tap Width interpolation. The small phase probe's maximum
pointwise error against the Fourier oracle is ~0.04271 for either bank;
mirroring reproduces the full bank, not necessarily the oracle.

`tools/audio_lab/MonoWidthAdaptiveKnots.h` greedily inserts the integer
Width with the worst normalized complex Fourier-coefficient error across
the two fixed band limits, four anchors and Morph blends. It starts from
nine endpoint/center-inclusive knots and reaches 17/33/65 frames; training
uses integer Widths and validation uses 101 held-out fractional Widths.
For this *coefficient-space* objective, worst held-out errors in dBc are:

| Frame budget | Uniform | Greedy | Gain |
|---:|---:|---:|---:|
| 17 | -5.95 | -9.09 | 3.14 dB |
| 33 | -10.96 | -12.67 | 1.71 dB |
| 65 | -23.41 | -27.41 | 4.00 dB |

The **matched playback** test uses the *same* 101 held-out Widths, both
pitch bands, all six Morph positions, 1x and 4x, identical cubic phase and
Width lookup, and the same reference/downsamplers for uniform and greedy
65-frame banks (2,424 points per bank). It does **not** confirm a worst-case
win: uniform is -30.49 dBc worst complex harmonic error at 94.5%, versus
greedy -28.51 dBc at 27.5%. Worst nonharmonic power is -134.63 versus
-134.86 dBc; max absolute RMS error is 0.000821 versus 0.001434. The greedy
coefficient-space objective is not a reliable playback improvement. Stop
refining adaptive placement for now. The examples at 90.5% square (-42.27
dBc harmonic error) and 49.9% Morph 2.5 (-86.21 dBc) are local wins, not
global convergence.

At fixed 65 uniformly spaced frames and 2048 phase samples, with unchanged
cubic phase lookup and a matched 15-Width 4x probe, **more polynomial Width
taps made the worst harmonic error worse**: 4-point -31.00, 6-point -27.09,
8-point -21.33 dBc, all worst at bin 37 / 5.5% / square. Their worst alias
scores stay between -137.7 and -137.1 dBc. The worst single-partial complex
error relative to total signal power is at H26: -39.31, -34.25 and -27.75
dBc for 4/6/8 taps respectively. This is an endpoint/high-partial
problem for local high-degree Lagrange stencils, not evidence that higher
order is universally harmful or that any 129-frame bank would be better.
Each two added Width taps requires four additional phase lookups across
the two Morph anchors, plus more interpolation arithmetic; CPU cost was
not benchmarked.

At the previously worst uniform-table point (4x, 867 Hz, 90% Width,
square), H1–H9 errors contribute under 0.13% of summed complex-harmonic
error power; H23–H27 alone account for roughly 64.6%. Example: H25 has
reference magnitude 0.02921, table magnitude 0.02391, and absolute complex
error 0.00533. Thus -35.42 dBc mostly describes upper retained partial
voicing error, not a fundamental hole. The zero-ish H20 instead has table
magnitude ~0.00318 versus reference ~0.000014. Listening thresholds remain
undetermined.

At *exactly* 50%, the two-pitch comparison against the same Fourier oracle
favors the table over the existing neutral renderer for triangle/saw/square:
at 1x, 3539 Hz, the saw table's alias/harmonic error is approximately
-137.5/-152.9 dBc, compared with legacy -25.1/-18.6 dBc; the square is
-138.9/-152.9 versus -25.4/-21.3 dBc. The sine is near-identical. The
legacy sound difference is therefore real and primarily reflects the
short-corrected waveform, not an incorrect table at the neutral Width.
Compatibility versus one continuous renderer still requires a product
decision; no production renderer, preset, or DC-policy change follows.

An isolated *coefficient-only* band-density probe confirms that Width
requirements depend on retained harmonic count. At seven fractional Widths
and four anchors, worst normalized interpolation error (uniform frames,
four-point Width Lagrange) is:

| Frames | 6 harmonics | 27 harmonics |
|---:|---:|---:|
| 17 | -24.10 dBc | -11.74 dBc |
| 33 | -40.76 dBc | -13.48 dBc |
| 65 | -61.35 dBc | -23.41 dBc |

This suggests fewer frames may suffice in high-pitch bands, **not** that
these counts meet a listening or production threshold. The numbers do not
include float table interpolation, playback, pitch-band switching, moving
Width, or CPU/cache cost; these remain separate validation tasks.

## Guarded pitch-band transitions (offline only)

The provisional static Width choice is **65 uniformly spaced 5–95% frames,
four-point Width Lagrange, and mirrored 5–50% storage**. It does not settle
the pitch dimension or constitute a production renderer. The table prototype
now has *opt-in* `spectralGuard` and `fadeFraction` constructor arguments;
their defaults (1 and 1) preserve the original hard-switch behavior and
earlier comparisons. In a three-band 48 kHz experiment, ceilings are
900/1800/3600 Hz, `spectralGuard=0.9`, and each fade begins at 80% of the
brighter band's ceiling and ends **at** that ceiling. The truncation uses
the highest fundamental that can use each band, with harmonic `H` satisfying
`H * ceiling < 0.9 * 24000 Hz` (also respecting internal Nyquist). The band
limits are 23/11/5 harmonics. Both tables are below the guard throughout a
fade; the brighter band is not played above its ceiling.

At the 900 and 1800 Hz boundaries, the hard-switch control has a sampled
phase jump as large as 0.231; the faded bank's two fade edges are continuous
within the tested `nextafter` precision. Coherent phase-cycle FFT and RMS
measurements show no measurable step at either fade edge for widths 10.5/90.5%
and Morph 1.5/3. A half-second phase-continuous glide gives a maximum
*pitch-induced* per-sample change below 3.1e-5 for the fade, compared with
up to 0.0362 for the hard switch over this grid. Ordinary sample-to-sample
changes are much larger (waveform slope/edges), so that is not an audible
click threshold. The glide compares the two policies at the same phase; its
broadband spectrum is **not** used as an alias estimate.

The static sweep is less reassuring: despite removing the hard jump, the
darker table removes retained high harmonics. Near the 900/1800 Hz boundaries,
matched coherent host-rate playback (1x and 4x with identical downsampling
for the Fourier target and table) reaches worst **complex harmonic error**
of -16.00/-13.29 dBc, max absolute RMS error 0.00725/0.01348 and max
time-domain RMS difference 0.0908/0.1229. Worst nonharmonic power is
-138.80/-139.02 dBc respectively. An independent phase-cycle sweep across
78–100% of each boundary agrees about the brightness discrepancy but its
"out-of-target harmonics" score is *not* host-rate alias power. Thus the
crossfade addresses switching continuity, **not** loss of high-partial
voicing; a safe overlap alone cannot restore harmonics omitted by the
darker band. Do not freeze the band scheme on these results. Explore
transition placement/brightness against a playback and listening target
before production. The last band above 3600 Hz, quality-dependent behavior
outside the 1x/4x checks, fast pitch movement, filter-open glides, dynamic
Width/Morph, CPU, and listening remain unvalidated. In particular, this
three-band bank does **not** provide an indefinitely safe top band: its five
retained harmonics exceed the 0.9 guard once pitch exceeds 4320 Hz and
cross host Nyquist above 4800 Hz. A usable full-range bank needs additional
darker bands (or an explicitly bounded pitch range); the tests do not
establish safety above the final 3600 Hz ceiling.

## Independent harmonic taper and pitch-level density (offline upper bound)

Pitch-band voicing now has its **own target**, separate from the unchanged
all-legal-harmonics Fourier renderer-correctness oracle. The experimental
guarded reference multiplies each complex Fourier coefficient `c[h]` by
`G(2*h*pitch / hostRate)`: `G=1` through 0.8 of host Nyquist, a cubic
smoothstep down to zero from 0.8 to 0.9, then `G=0`. These thresholds are
illustrative, **not** a selected downsampler/production safety policy. DC is
unchanged. In this experiment, the fundamental itself fades from 19,200 to
21,600 Hz; above that, the output contains only its original DC component.
The independent envelope allows *several* high harmonics to taper at once.

The opt-in table prototype accepts explicit harmonic limits and constructs
levels H32, H31, ... H1, H0 (DC-only); at any pitch it adds guarded
**differences between adjacent levels** to the DC-only level, instead of
crossfading just one active pair. This is equivalent to independently
tapering each harmonic at exact Width frames, before phase/Width lookup
error. The test checks overlapping H22/H23 tapers, the H1→H0 endpoint, and
the H32→H0 ordering. The bank is an *upper-bound diagnostic*, not a
real-time proposal: it requires multiple level lookups during overlap and
stores 35,684,352 sample bytes for 33 mirrored Width frames, 2048 phase
samples, four anchors and 33 levels. Its finite H32 first level is tested
only from 675 Hz upward: lower fundamentals would need additional harmonics.

Using that **same** guarded target and coherent 48 kHz playback with matched
1x/4x downsampling, 12 pitch bins (726–5391 Hz), two off-center Widths and
two Morph settings, the pitch-level-density probe measures:

| Level spacing | Levels incl. H0 | Sample bytes | Worst complex harmonic error | Worst nonharmonic power |
|---|---:|---:|---:|---:|
| 1 band/octave | 7 | 7,569,408 | -11.30 dBc | -138.20 dBc |
| 2 bands/octave | 9 | 9,732,096 | -11.30 dBc | -138.20 dBc |
| 4 bands/octave | 14 | 15,138,816 | -26.72 dBc | -138.78 dBc |
| 1 harmonic per level | 33 | 35,684,352 | -38.90 dBc | -138.36 dBc |

The one-/two-band worst case is the same 5391 Hz / 10.5% / square / 1x
point; the four-band worst is 867 Hz / 10.5% / square / 4x; the one-harmonic
worst is 727 Hz / 90.5% / square / 1x. Coarser grouped levels fade *all*
newly omitted harmonics when the highest one approaches the guard, so their
worst-case score reflects premature darkening, not a discontinuity. These
are sample-data bytes only; vector metadata, build time, runtime cost,
pitch glides, lower notes, high-note audibility, modulation sidebands and
filter-open listening are **not** included. Static phase/Width lookup error
still contributes even with per-harmonic levels. Do not select a practical
band density from this sparse playback grid alone; use it to direct the
next fuller pitch sweep and listening comparison.
## Full-range pitch-level sweep and listening fixtures (offline)

`tools/audio_lab/MonoWidthPitchLevels.h` models the grouped bank in
**coefficient space**: telescoping adjacent levels gives each harmonic in
`(limit[l+1], limit[l]]` the guard gain of `limit[l]`, so a group fades with
its highest member. This isolates level spacing from Width/phase lookup
error and from the table's finite first level. `monoWidthLevelLimits` is now
the one schedule used by both the model and the table tests. Checked against
the 4 bands/octave (bpo) H32 table bank, per-harmonic spectra agree within
1.8e-3 (the 33-frame lookup error). Matched playback agrees within 0.05 dB
at 1x/2x/4x/8x; e.g. 867 Hz / 10.5% / square predicts -26.74 and
measures -26.72 dBc. The earlier -26.72 dBc figure is therefore **entirely
level spacing**. Matched downsampling changes the ratio by <0.01 dB, so the
spacing question does not depend on Quality while the guard is defined at
host rate.

The sweep runs from the lowest pitch a 2048-sample table fully covers
(`0.9 * Nyquist / 1023`) to the guard. It uses 24 points/octave plus every
level's fade start/middle/end, 11 Widths (5–95%, including 7.5/33.3/66.7/92.5),
Morph 0–3 in 0.5 steps, and 44.1/48/96 kHz, with levels starting at H1023:

| Spacing | Levels | Worst dBc (48 kHz) | Worst at | Lowest darkened partial 44.1 / 48 / 96 kHz |
|---|---:|---:|---|---:|
| 1 bpo | 11 | -6.52 | 7215 Hz / 80% / square | 9.1 / 9.9 / 19.8 kHz |
| 2 bpo | 19 | -10.00 | 5405 Hz / 50% / square | 12.9 / 14.0 / 28.0 kHz |
| 3 bpo | 27 | -14.55 | 3608 Hz / 10% / square | 14.4 / 15.7 / 31.3 kHz |
| 4 bpo | 34 | -17.59 | 2703 Hz / 50% / square | 15.2 / 16.6 / 33.2 kHz |
| 6 bpo | 47 | -19.96 | 2145 Hz / 50% / square | 16.2 / 17.6 / 35.2 kHz |
| 8 bpo | 60 | -24.80 | 1517 Hz / 50% / square | 16.6 / 18.1 / 36.2 kHz |

"Darkened" means more than 1 dB below the guarded target. The one-harmonic
ladder is exactly zero in this model. **The sparse 12-bin grid understated
4 bpo by 9 dB.** Its true worst case is a few-partial high note. At 2703 Hz
the {7, 8} group fades with H8, removing the square's H7 (18.9 kHz)
while it is still at 0.79 Nyquist. By octave from 20 Hz, 4 bpo worst
error at 48 kHz is -39.4, -36.3, -33.4, -30.1, -26.1, -23.0, -19.9 and
-17.6 dBc. It then reaches ~0 above ~5 kHz, where the levels are already one
harmonic apart (H5, H4, ...). Bass is the *easy* case in dBc: roughly 3 dB
per octave lower, because the 1/h spectrum puts little power in the
affected band.

**The dBc score does not reflect audibility.** Every grouped-bank error lies
between the darkened-partial frequency and the guard. For 4 bpo that is
above ~15.2 kHz at 44.1 kHz and ~16.6 kHz at 48 kHz, and wholly ultrasonic
at 96 kHz (where even 1 bpo is). Grouping acts as a pitch-dependent
lowpass whose start oscillates between ~0.69 and 0.8 of Nyquist; it is
not broadband voicing error. The listening question therefore reduces to
whether removing ~15–19 kHz partials early is audible at 44.1/48 kHz.

Two consequences for the table design, not yet tested:

* A 2048-sample first level (H1023) covers every guard-passing partial only
  above 19.4/21.1/42.2 Hz at 44.1/48/96 kHz. Below that, content above
  `1023 * pitch` is missing (e.g. 10.2 kHz at 10 Hz). The oscillator reaches
  roughly 1 Hz (MIDI 0, 16', -24 st), so sub-audio behavior needs a policy.
* If early removal is audible, one alternative to the hybrid partial approach
  is to start each group's fade when its *lowest* member reaches 0.8. That
  moves the error into the guard band instead: at 4 bpo the top member reaches
  ~0.95 Nyquist. This changes guard placement, not fade width, and depends on
  the real downsampler.

`VektMonoWidthPitchLevelListening <dir>` (audio-lab presets) writes 36
exact additive-synthesis fixtures per sample rate (44.1/48 kHz). There are
no tables, no lookup error and no oversampling, so variants differ only in
partial fading:

* `-oracle`: independent per-harmonic taper.
* `-4bpo`: the candidate, RMS-matched to the oracle.
* `-2bpo`: control, RMS-matched.
* `-diff-4bpo`: unmatched 4bpo minus oracle, i.e. exactly what the candidate removes.

Fixtures are 12 s exponential glides (square 1.5–6 kHz; saw 220–1760 Hz;
Morph 2.5 / 90% at 30–240 Hz; 5% square 1–4 kHz) and 4 s static notes. The
static notes sit at the 4 bpo model-worst pitch in 1–8 kHz (square, saw,
5% square) and 30–120 Hz (saw; Morph 2.5 / 90%). `fixtures.csv` records
pitches, model and measured difference levels, darkening frequency and match
gains; unmatched static differences match the model within 0.005 dB, and
their content below 15 kHz is at the -100 to -150 dBc numerical floor.
(RMS-matching the candidate before subtracting would add a broadband
`(gain-1)` copy of the note, about -41 dBc, so the diff files are unmatched.)
Files are mono 32-bit float. At the high static
points 2 bpo and 4 bpo happen to coincide, so use the high square glide
(worst 4 bpo -17.6 vs 2 bpo -10.0 dBc)
as the audible control. **No listening has been done yet.** Nor are the
ladder, moving Width/Morph, modulation sidebands or CPU covered.

### Through the production ladder

The static high square and the square, 5% square and Morph 2.5 / 90% glides
are also rendered at 44.1 kHz (the lowest darkening frequency) into
`ladder/`. Each goes through `NonlinearTptLadder::processCoupled` at 1x and at
4x FIR with Mono's `OversamplingBank` decimator. Settings: cutoff 12 kHz,
resonance 0.4, Drive 0/+12/+24 dB. Oracle and candidates enter the ladder
with **identical, unmatched** gain staging. Output files are RMS-matched to
the oracle for listening; diff files and metrics use the unmatched
difference. `ladder/ladder.csv` has every row. The range across 1x/4x and
all three Drives:

| Fixture | 4 bpo difference below 15 kHz | 2 bpo difference below 15 kHz |
|---|---:|---:|
| static high square (2484 Hz) | -26.4 to -37.1 dBc | same (levels coincide) |
| high square glide | -34.2 to -45.5 dBc | -20.6 to -34.5 dBc |
| 5% square glide | -31.9 to -47.0 dBc | -25.4 to -37.1 dBc |
| Morph 2.5 / 90% bass glide | -51.0 to -57.0 dBc | -44.2 to -49.3 dBc |

**The ladder does move the difference into the audible band.** Before it,
the grouping changes nothing below 15 kHz; after it, nearly all of the
difference lies below 15 kHz, because the 12 kHz lowpass removes the missing
partial itself. The argument "it is above 16 kHz, so it cannot be audible"
therefore does not hold for driven/filtered patches. On the static note the
output is periodic, so this difference can only be changed lower
harmonics. Their **levels** move by at most 0.61 dB (1x, +12 dB). The rest is
harmonic phase: at +24 dB the level change is 0.07 dB at 1x and 0.005 dB at
4x, despite -26 to -34 dBc total. The worst glides remain 6–15 dB below the
2 bpo control. Numbers are not listening results.

### Current decision state (pending listening)

* Static Width: 65 uniform frames, four-point Width Lagrange, mirrored
  half-bank.
* Pitch levels: **4 bpo is the leading configuration.** 6 bpo buys ~1 kHz of
  darkening frequency and ~2.4 dB for 47 instead of 34 levels, so it is
  not a useful tier. 8 bpo (60 levels) is the fallback if 4 bpo audibly
  fails. No further numerical optimization of the pitch bank before
  listening.
* Listening order: 44.1 kHz static high square, then high square glide, 5%
  glide, Morph 2.5 / 90% glide; repeat informative cases at 48 kHz and
  through `ladder/`. Compare oracle vs 4 bpo blind and level-matched first,
  then 2 bpo as control. Accept 4 bpo if differences are at most
  detectable, not musically significant (duller tone, brightness undulation or
  stepping during glides).
* Lowest-member fading is demoted. It swaps conservative early treble loss
  for partials up to ~0.95 Nyquist. Consider it only if 4 bpo fails, and only
  with measurements against the real decimator.
* Sub-20 Hz first-level coverage is a separate low-frequency policy question
  (what fundamentals below ~20 Hz are for), not a pitch-density one.

## Listening result: the shared warp is the wrong Width target

Listening at full Width found sine and triangle almost identical and too
bright, close to saw, and the three non-square shapes not distinct enough from
square. This concerns the **Width definition**, not the renderer, so the
pitch-level decision above is paused: its numbers apply to the old
shared-warp target and must be rerun once Width families are chosen.

Candidate model: one user Width, anchor-specific depth
`d_s = 0.5 + k_s * (width - 0.5)`, with square at `k = 1` (true PWM). Each
anchor is generated with its own warp and **time-shifted so its fundamental
stays at +sin phase at every Width**; Morph then blends these anchors.
A since-removed `VektMonoWidthFamilyListening` tool rendered exact, guarded, DC-free additive
fixtures at 220 Hz / 48 kHz with equal nominal gain (the warp preserves
each anchor's RMS). The sets are shared (all `k = 1`, the current prototype),
mild (0.25/0.45/0.65/1), medium (0.35/0.55/0.70/1) and strong
(0.45/0.65/0.80/1) for sine/triangle/saw/square. `families.csv` records
the metrics below at Width 50/75/95%. Centroid is the power-weighted mean
harmonic number. Distance is RMS over H1–H30 of each spectrum's
power-normalized dB levels, floored at -60 dB.

| Set @ 95% | Centroid sine / tri / saw / square | Distance sine–tri / tri–saw / saw–square | Morph H1 minimum, unaligned |
|---|---|---|---:|
| (any set @ 50%) | 1.00 / 1.04 / 3.12 / 2.36 | 12.8 / 29.5 / 25.7 dB | 1.00 |
| shared | 2.09 / 2.00 / 5.76 / 8.65 | 5.2 / 6.5 / 8.0 dB | 0.80 |
| mild | 1.03 / 1.13 / 3.89 / 8.65 | 10.3 / 24.1 / 8.1 dB | 0.84 |
| medium | 1.06 / 1.19 / 4.05 / 8.65 | 9.9 / 23.1 / 8.1 dB | 0.84 |
| strong | 1.10 / 1.27 / 4.46 / 8.65 | 11.6 / 22.4 / 8.0 dB | 0.84 |

The numbers agree with the listening. Under the shared warp every distance
collapses at 95%, and sine and triangle become the closest pair with equal
brightness. All reduced sets keep sine/triangle far from saw. Saw–square
stays ~8 dB in every set because the square itself is unchanged: a 95%
pulse is the brightest anchor (centroid 8.65). The unaligned warp loses up
to 20% (-1.9 dB) of the fundamental mid-Morph at 95% in the current
prototype. Per-anchor alignment makes the fundamental exactly the weighted
sum of its endpoints (1.00); higher-harmonic cancellation is not measured.
At these depths sine barely changes (centroid ≤1.10). If it sounds too
subtle, a Fourier-defined sine family (dominant H1, clear H2, fast rolloff)
is the next candidate, rather than a stronger warp.

Files: `compare-<anchor>-w95-shared-mild-medium-strong.wav` (the quickest
A/B), per-set `anchors-w50/75/95.wav` (sine, triangle, saw, square in
sequence), `<anchor>-w50-75-95.wav`, `morph-sweep-w95.wav` (Morph 0→3 over
12 s), and `shared/morph-sweep-w95-unaligned.wav` (current prototype).
Acceptance: at maximum Width all four anchors are immediately identifiable
yet audibly transformed, and a Morph sweep passes four distinct landmarks.
No production change follows until listening picks a set.

## Decision: strong depths with a two-tooth saw (29 September 2026)

Audio Lab listening preferred **strong** (sine 0.45, triangle 0.65, square
1.0), except that the 0.8 saw warp was nearly inaudible: 95% sounded like
50%. Bending the ramp mostly changes H1–H4, while the saw's reset still
dominates with its unchanged 1/h series. The saw is therefore no longer
warped. Width now blends two neutral polyBLEP saws offset by ±offset/2 cycles.
The offset runs linearly from 0 at 50% to a quarter cycle at 5/95%. Harmonic
`h` is scaled by `cos(pi h offset)`, a comb whose notches move with Width; at
the extreme H2/H6/… vanish and H1 keeps 0.71. The fundamental stays at +sin
phase by construction. Level is divided by `sqrt(1 - 3 offset + 3 offset^2)`
so the saw's RMS, and thus ladder drive, is constant. A full-strength saw
warp was also offered and not chosen.

This is now the only Width model. `MonoVoice::widthAnchor()` implements it
per anchor (`anchorWidthDepths` = 0.45/0.65/–/1.0, `WidthAlignment`, and
`twoToothSawOffset`/`twoToothSawGain`), and `waveform()` Morphs between
those anchors. Exactly 50% Width is still the legacy renderer. The other
families, the Audio Lab selector and the family-listening tool were removed.
`anchorWave()` remains the unaligned phase-warp primitive: the geometry and
renderer-comparison tests compose it into a local shared warp, so their
results above still describe that renderer, not the shipped Width.

Consequences and open items:

* Any patch or factory preset with Width off 50% sounds different: new sine,
  triangle and saw shapes, and time-aligned anchors. Presets have not been
  re-voiced.
* The live renderer is still the short BLEP/BLAMP prototype, so the chosen
  sine/triangle warps inherit its known antialiasing limits. The two-tooth saw
  needs no new events.
* The offline reference, Width-frame density and pitch-level results above
  were measured on the shared warp. They must be rerun on this target before
  a table renderer is selected. The two-tooth saw's coefficients are
  `c_h(saw) * cos(pi h offset) * gain`, so the reference extends easily.
* Width modulation of the two-tooth saw is a moving comb; it has not been
  checked for zipper or sideband behavior.

### Pitch-level listening re-targeted to the shipped Width

`monoWidthAnchorCoefficients` / `monoWidthShippedCoefficients` in
`MonoWidthReference.h` now give independent Fourier coefficients for the
shipped model: exact alignment rather than the production table, and the
two-tooth saw as `c_h * cos(pi h delta) * gain`. A test checks H0–H12 against
a dense DFT of `MonoVoice::widthAnchor` within 2e-3 at 5/20/50/63.7/95% for
every anchor. The full-range sweep and `VektMonoWidthPitchLevelListening`
now use this target, and the listening set adds a 1.5–6 kHz two-tooth saw
glide at 95% (clean and through the ladder).

Level-spacing conclusions are unchanged: worst 4 bpo is still -17.59 dBc
(50% square at 2484/2703 Hz), 2 bpo -10.0 dBc, and darkening starts at
15.2/16.6 kHz (4 bpo, 44.1/48 kHz). One new ladder case stands out: the
two-tooth saw glide at +24 dB Drive leaves -23.7 dBc (1x) / -25.1 dBc (4x)
of 4 bpo difference below 15 kHz, versus -15.0 / -18.7 dBc for 2 bpo. That
is the least favorable ladder result for 4 bpo, so listen to it first.

## Live listening result: pitch-level policy (29 September 2026)

Audio Lab (lab-only `MonoOscillatorOverride` hook, `MonoTableOscillator`) plays the shipped
Width through the real voice as production, table per-harmonic reference, table 4 bpo or
table 2 bpo. The table renderer builds every anchor from two Width-independent primitives (saw
for value jumps, parabola for slope corners) plus a 24-harmonic sine residual; it matches the
exact additive renderer within 5e-5 over 25 Hz–21 kHz, 5–95% Width, all Morph positions, and
44.1/96 kHz, using 8.8 MB. One oscillator costs about 35–95 ns per sample at 1x for 4/2 bpo
(including a Morph LFO); production costs about 5 ns.

Listening: with a clean oscillator the four modes were indistinguishable. With osc 1 only,
Morph 3, Width 5%, Octave +2, C5–C6 with vibrato at **Quality 1x**, all modes except 2 bpo
whistled. At **Quality 2x** production still had audible artifacts and the three table modes
did not; at **4x** all four sounded the same.

Measurement (`VektMonoAdditiveCost`, 44.1 kHz, static C5/G5/C6, whole processor) explains it.
Inharmonic output power is:

| Quality | Production | Table reference | Table 4 bpo | Table 2 bpo |
|---|---:|---:|---:|---:|
| 1x | -18 to -31 dB | -28 to -35 dB | -29 to -35 dB | -30 to -38 dB |
| 4x | -37 to -47 dB | -43 to -57 dB | -44 to -57 dB | -45 to -57 dB |

The table oscillator itself is alias-free, so at 1x the whistles come from the ladder's
saturation running at the host rate (even with the filter open and 0 dB Drive). 2 bpo only
feeds it less treble from ~13 kHz up. From 2x on, no policy difference was audible.

Whole-processor cost (default patch, one held A2, all three oscillators sounding) is 2.8 /
5.6 / 10.6% of a core for production at 1x / 2x / 4x and 4.8 / 9.7 / 19.0% for table 4 bpo.
Equal audible cleanliness therefore costs about the same: table 4 bpo at 2x (9.7%) versus
production at 4x (10.6%).

**Decision:** the pitch-level density is not audible in this listening; keep **4 bpo** as the
table policy (smaller worst-case voicing error than 2 bpo, and without 2 bpo's pitch-dependent
brightness steps). Audible aliasing at Quality 1x is a separate product decision: default
Quality, a narrower 1x-only oscillator guard, or oversampling the ladder alone at 1x.

## Production switch: table oscillator, 2x default (29 September 2026)

Mono's voice now renders every oscillator with `renderWidthOscillator`
(`plugins/vekt_mono/Source/WidthOscillator.h/.cpp`), the 4 bpo version of the lab table renderer:

* `WidthWavetable` (shared, sample-rate independent, 4.2 MB): per pitch level a saw and a parabola
  primitive (at least 16 samples per cycle of the level's top harmonic, cubic lookup) and 24-harmonic
  sine-residual tables over 65 Width frames. Built once (~0.3 s), touched from `MonoVoice::prepare`.
* `WidthOscillatorState` per unison layer and oscillator caches the anchor shapes (primitive events,
  DC, residual stencil) for the current Width; Morph only reweights two of them.
* The guard is defined at the host Nyquist: `MonoVoice::prepare` takes the host rate, which the
  processor passes alongside the oversampled rate. `WidthDcPolicy::zeroCentered` drops the anchor DC.
* Silent oscillators (level 0 after modulation) are skipped; their phase still advances.
* `widthHarmonics` (exact analytic coefficients) is production code; the offline tools call it, and
  a test checks it against the independent FFT reference. Another test checks the production
  oscillator against the exact additive 4 bpo renderer (worst error 5e-5 at 44.1/96 kHz).

Quality now defaults to **2x** (IIR, 4 samples latency). Presets do not store Quality. The processor
now splits oversampled render segments to the prepared block size; previously a host block larger
than `prepareToPlay`'s maximum overran the oversampler at 2x/4x/8x.

Cost (`VektMonoAdditiveCost`, M-series, 48 kHz): one oscillator 50–95 ns per sample static or with a
Morph LFO, ~160 ns with a Width LFO (legacy BLEP ~27 ns); whole processor, default patch, one note:
4.7 / 9.4 / 18.3% at 1x / 2x / 4x. The whistle setup measures -49 to -57 dB inharmonic at 2x.

Consequences: neutral (50%) Width is now the exact bandlimited waveform rather than the legacy
polyBLEP shape, and patches load at 2x unless the host session stored Quality. The Audio Lab
oscillator selector and lab table class were removed.

The legacy polyBLEP/polyBLAMP renderer (`MonoVoice::waveform`, `anchorWave`, `widthAnchor`, the Width
event corrections and `WidthAlignment`) has been deleted. Oscillator tests now exercise
`renderWidthOscillator`: phase alignment, anchor RMS and shapes (within the bandlimit's ripple), Morph
weights and continuity, DC policies, full-depth PWM duty cycle, the two-tooth saw, agreement with the
independent Fourier reference, and a coherent alias check at 3.5 kHz (-121 to -138 dB between
harmonics, versus -4 to -31 dB for a naive shape). Renderer-comparison diagnostics that only
compared against the BLEP corrections were removed; their results above are historical.

## Review follow-up: Width smoothing and factory preset re-voicing

Width knob and automation changes now ramp over 10 ms, like Morph (LFO Width stays per sample; its
output is already de-clicked). An instantaneous 5 -> 95 % jump on a held sine stepped the output by
0.335 against a steady maximum of 0.03; with the ramp the largest step is 0.030
(`Mono Width knob jumps ramp instead of stepping the oscillator`).

Saved sounds with an off-center Width change timbre under the new model; the choice was to re-voice
the factory presets rather than migrate. All affected presets are sound schema 4 without LFOs. The
original voicing target is the old pulse-only Width: an unwarped saw blended with a +-g pulse of
duty Width. Per oscillator:

* Morph <= 2 (no pulse content, so the old Width did nothing): Width 50 restores the sound exactly.
  Classic Three Bass osc 3 (was W42), Aurora Lead osc 1 (was W44).
* Morph 3 (pure pulse): unchanged; the duty cycle is the same as before.
* Saw/pulse blends: Morph and Width searched jointly to minimize the RMS difference of power-normalized
  harmonic levels H1-H40 against the old sound (floored at -60 dB). Level change is within 0.4 dB.

| Preset / osc | Was | Now | Distance (was if kept) |
|---|---|---|---:|
| Aurora Lead osc 2 | M2.15 W58 | M2.03 W50.5 | 0.70 (8.92) dB |
| Circuit Lead osc 1 | M2.85 W24 | M2.96 W24 | 0.60 (0.85) dB |
| Circuit Lead osc 2 | M2.10 W68 | M2.00 W50 | ~0.4 (8.63) dB |
| Current Motion osc 1 | M2.35 W62 | M2.05 W51 | 2.01 (7.91) dB |
| Tide Motion osc 3 | M2.70 W64 | M2.82 W64 | 2.02 (2.95) dB |
| Nocturne Pad osc 1 | M2.75 W36 | M2.83 W36 | 1.50 (3.34) dB |
| Ember Pluck osc 3 | M2.60 W34 | M2.83 W34 | 2.02 (5.08) dB |
| Rubber Bass osc 2 | M2.72 W30 | M2.76 W30 | 2.47 (2.76) dB |
| Transmission FX osc 1 | M2.85 W30 | unchanged | 0.63 dB |

This matches static harmonic magnitudes only; it does not reproduce the old fundamental cancellation
between unaligned anchors or any listening judgment. The presets still need auditioning.

## Factory presets: first LFO voicing (draft, to audition)

The factory set predated the LFOs (all sound schema 4). Twelve presets are now stored as schema 7:
upgraded exactly as the loader migrates schema 4 (`notePriority` 0, every LFO/vibrato parameter at its
default), then given draft settings. Motion and Pad presets use the LFOs, favoring Width (two-tooth comb
and PWM), Morph and filter; the Leads tune the mod-wheel vibrato that every migrated preset already had
(50 ct at 5.5 Hz), and Aurora Lead adds a delayed automatic vibrato. Basses, plucks and FX are unchanged.
A test loads every factory preset through the preset session and checks that schema-7 values land.
These are numerical drafts, not listening decisions.

## Quality default back to 1x

Because of overall CPU cost, Quality defaults to **1x** again (zero latency; 4.7% of a core for the
default patch with one note, versus 9.4% at 2x). The table oscillator stays alias-free at 1x; what returns
is the ladder's own aliasing on bright high notes (the whistle setup measured -18 to -35 dB inharmonic at
1x, inaudible from 2x). The Quality tooltip says so. The processor keeps splitting oversized blocks for
2x/4x/8x, and the tests that need sample-exact 1x behavior still select it explicitly.

## CPU optimization pass

Profiled with `sample` on `VektMonoProcessorCost 48000 128 8 1` (8 held voices, 1x, first factory preset,
resonant driven filter); each change was A/B-timed against the previous build.

| Change | Median µs per 128-sample block |
|---|---:|
| Before | 530 |
| Merge coinciding primitive reads, drop zero-weight ones (e.g. neutral-Width saw teeth, triangle/sine corners) | 490 |
| Guard samples in every table (no wrap mask), compact limit array | 485 |
| Skip zero-weight sine-residual Width frames (a Width frame such as 50 %) | 470 |
| O(1) pitch-level selection (`firstLevelAtMost`) and one reciprocal per sample | 387 |
| Velocity curve cached per note, drive gain cached per ladder, one `exp2` per oscillator pitch | 384 |

Overall -28% (p99.9 693 -> 506 µs); the default patch with one note costs 3.2% of a core at 1x
(was 4.7%; the legacy renderer was 2.8%). Output is unchanged within the existing oscillator accuracy
test (worst 5e-5 against the exact additive renderer). Tried and reverted: a per-oscillator pitch-level
cache (pitch changes every sample with drift or vibrato, so it only added work) and a vector `tanh` for
the single-ladder solve (no measurable gain). The remaining profile is roughly one third each: the ladder
(`tanh`), the voice/processor loop, and the oscillator's table reads.
