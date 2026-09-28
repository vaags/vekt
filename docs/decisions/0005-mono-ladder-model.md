# ADR 0005: Mono Ladder Model

## Status

Proposed

**Implementation decision (28 September 2026):** Coupled is now Mono's sole
render-path ladder at 1x/2x/4x/8x, including the ordinary host entry point.
This pre-alpha breaking sound change is intentional: there are no users or
sound-compatibility commitments. This is an implementation decision, **not**
acceptance of the model for release. The numerical, listening, CPU, allocation,
latency and host gates below remain open; a failed gate blocks release or
requires a reviewed model/scope change, not an automatic legacy fallback.
Earlier dated development results and cutover proposals below are historical
evidence, not instructions to maintain a second engine.

## Context

**Scope revision (27 September 2026):** Mono no longer offers or plans 16x.
The retained quality range is 1x/2x/4x/8x for the current single control and
for both planned playback and offline controls (offline follows playback by
default). Stored index 4 project states are rejected rather than recalled at
8x; other indices are unchanged. Historical 16x diagnostic measurements and
earlier offline-host policies below are superseded as product requirements.
The removal is motivated by CPU concern, not a demonstrated CPU conclusion
from short probes. Remaining paths still require their own validation.

Vekt Mono needs one authoritative ladder definition before candidate tuning or
production integration. The delayed-feedback implementation is comparison context,
not a response target. The current development candidate combines a Huovilainen-style
four-stage nonlinear ladder with trapezoidal integrators and a bounded Newton solve
for the delay-free feedback loop. Huovilainen's DAFx-04 paper and Zavalishin's
*The Art of VA Filter Design* inform the structure and numerical method, but neither
source specifies this exact combined model.

This record specifies the ladder now implemented in Mono. Proposed status means
the equations are precise enough for absolute-reference testing and the product has
approved the ladder/gain boundary below, but the complete validation matrix, final
acceptance tolerances and production fallback have not yet been approved.

**Historical provisional pre-alpha integration decision (27 September 2026;
superseded 28 September 2026):** With no
external users or compatibility obligation, the candidate may be exercised
inside Mono's actual voice and processor *before* this ADR is Accepted. This
does not authorize a released candidate engine or waive finite output,
determinism and basic processor smoke tests. The initial integration moves
the one-step solver into `VektMonoCore` and permits selection solely via an
explicit C++ development-build constructor at 1x. The normal host entry point
and other factors, including 16x, remain on legacy; the existing quality
parameter/state contract is unchanged. This is an intentionally limited
development experiment, not the final separate playback/offline controls.
Record results and regressions in `docs/MONO_LADDER_ACCEPTANCE_PLAN.md`.

**Historical replacement direction (27 September 2026; superseded by the
implementation decision above):** Thomas chose the coupled
Newton solver of the same four implicit stage equations as the preferred
candidate for replacing the legacy Mono render engine, with legacy removal as
soon as appropriate after acceptance and verified production integration.
The explicit objective is a single coupled production engine across all retained
1x/2x/4x/8x modes, not permanent coexistence with legacy. Qualify 1x first,
then the higher modes; that order does not authorize a 1x-only release.
The nested solver remains a numerical comparator; neither solver is approved
for the shipped host path yet. This choice does not approve coupled sound,
CPU/safety, all retained quality modes or host
behavior. Do not switch the host entry point or delete the legacy implementation
before those gates and cutover regressions pass. No permanent mixed-engine
release is intended; any narrower release scope requires an explicit ADR
0001/0005 revision, not a silent substitution.

**Development audition report (27 September 2026):** Thomas reports that he
tested coupled 1x, finds it sounds much better than legacy, and thinks legacy
can be removed. This establishes a positive, unblinded 1x sound preference,
not documented control-by-control observations or acceptance of higher-quality,
CPU/safety and host paths. On 27 September 2026 Thomas superseded the
26 September blinded-listening protocol: documented unblinded checks on
representative patches and settings are required, but randomization,
level-matched three-repeat comparisons and blinding are not release gates.
Use a blinded comparison optionally if a sound decision becomes uncertain.
At the time of this audition the normal plugin still used legacy and
development-only coupled 2x/4x/8x paths existed. This report does not qualify
the replacement. Keep this ADR Proposed until retained paths pass the gates below.

**Preset/project policy (28 September 2026):** Accept existing supported Mono
filter parameters and stored quality indices 0–3, but interpret them through
the coupled model. Their values are recalled, **not** their former sound.
Stored quality index 4 (16x) project states remain rejected as a whole; host-
managed normalized parameter snapshots may not be interceptable and must be
checked in real hosts. Factory and user presets require listening/headroom
review on the new engine. Retain the pre-change Git revision and any comparison
renders as historical references, not a runtime selector or production SKU.
The separate Playback/Offline Quality controls planned by ADR 0001 remain a
release-contract decision; this implementation keeps the existing single
1x/2x/4x/8x control and does not claim that split is implemented.

## Product boundary

Mono uses a hybrid product direction: a classic ladder/resonance identity with modern,
explicitly separated sound-design features. The raw ladder output is `y4`. Drive raises
the actual input to the nonlinear ladder and is not implicitly normalized away.

Optional drive compensation is a separate, bypassable post-ladder wrapper. Optional Q
compensation is also post-ladder and defaults off. Neither compensation participates in
the state equations or feedback loop. Any future output-feedback tap precedes both.
Classic oscillator-3/noise modulation and final monophonic articulation are deferred to
their later product phases and do not block validation of the ladder itself.

## Candidate equations

All internal signals are normalized and dimensionless. Let `x` be the external input,
`D = 10^(driveDb / 20)` the drive gain, `r` the normalized resonance control, and
`k = 4 clamp(r, 0, 1)` the feedback gain. The instantaneous first-stage input is:

```text
u1 = clamp(D x, -24, 24) - k y4
```

For stages `i = 1..4`, with `u(i) = y(i-1)` after the first stage, the selected
continuous-time candidate is:

```text
dy(i)/dt = wc [tanh(u(i)) - tanh(y(i))]
```

The output tap is the raw fourth stage:

```text
outputRaw = y4
```

The development harness can additionally measure the approved optional wrapper:

```text
outputCompensated = y4 / sqrt(D)
```

Reports must label which output is measured. Raw output is the default for model
validation; compensated output is a separate sound-design candidate.

## Cutoff and resonance definitions

The cutoff control is clamped to `10 Hz .. 0.45 sampleRate`. For a processing rate
`fs`, the trapezoidal integration gain and equivalent prewarped continuous-time pole
frequency are:

```text
g  = tan(pi cutoffHz / fs)
wc = 2 fs g
```

In the small-signal limit, one stage is `wc / (s + wc)`. Four stages under feedback
have the transfer function:

```text
L(s)    = [wc / (s + wc)]^4
Hraw(s) = D L(s) / [1 + k L(s)]
Hcomp(s) = sqrt(D) L(s) / [1 + k L(s)]
```

At zero drive gain offset and `k = 0`, the control identifies the prewarped one-stage
pole, not the aggregate ladder's -3 dB point. In the ideal small-signal continuous
model, `k = 4` reaches the oscillation boundary at `wc`; therefore normalized
resonance `r = 1` is the intended nominal self-oscillation threshold. Nonlinear
amplitude limiting and numerical bounds alter the finite-amplitude behavior.

## Discretization

Each stage uses a topology-preserving trapezoidal integrator. Given integrator state
`s(i)` and instantaneous stage input `u(i)`, the new stage output solves:

```text
y(i) - s(i) - g [tanh(u(i)) - tanh(y(i))] = 0
s(i)' = 2 y(i) - s(i)
```

The exact linearized digital one-stage response is:

```text
H1(z) = g (1 + z^-1) / [(1 + g) + (g - 1) z^-1]
```

The four-stage closed-loop response is `H1(z)^4 / [1 + k H1(z)^4]`, multiplied by
`D` for raw output or `sqrt(D)` only when the optional compensation wrapper is enabled.

For validation, the physical continuous-time response evaluates `H(s)` at
`s = j 2 pi f`. The exact bilinear counterpart instead evaluates the same prototype
at the mapped probe frequency `s = j 2 fs tan(pi f / fs)`. The latter must equal the
closed-form digital response; the former quantifies the remaining frequency-axis warp.

Under the revised Mono-specific ADR 0001 scope, the *candidate release*
has separate Playback Quality (1x/2x/4x/8x, default 1x) and Offline Render
Quality (follows playback by default; explicit override allows
1x/2x/4x/8x) settings. The interim legacy engine has a single
four-choice setting. The candidate equations apply at the active internal
processing rate; every retained candidate choice requires validation,
**not** automatic acceptance at any factor. Existing 16x development probes
are historical diagnostic evidence only. The candidate
remains outside the production render path.

## Nonlinear solver and bounds

The development candidate keeps its state and Newton calculations in double precision
and returns float samples. Each stage solves the same monotone trapezoidal equation with
at most 24 safeguarded Newton iterations and an absolute residual of `2e-7`. Its root
is bracketed by the prior stage state plus or minus `2g`, since the difference between
two hyperbolic tangents is at most two. Newton steps that leave the bracket or travel
more than half its width are replaced by bisection.

The global solve uses at most 16 similarly safeguarded iterations with the same
residual bound. It evaluates `(u1 - clampedDrivenInput) + k*y4` to avoid subtracting
large nearly equal quantities. The global bracket covers the driven input plus or minus
`k*(24 + 2g)`, using the bound on the fourth-stage state and its integration term.
External driven input and updated states are clamped to `[-24, 24]`; stage and feedback
iterates are permitted outside that interval when the unconstrained equation requires
it. No global iterate is clipped to a bound that would exclude the equation's root.

The static supported-range solver test now reports zero unconverged samples on its
enumerated 256-sample probes, including the previously failing 44.1 kHz, 10 Hz,
resonance 0.5, input peak 4.0, +24 dB case. This is limited evidence, not proof of
zero fallback incidence on the complete stimulus and modulation matrix.
An additional 810-case, 8,192-sample-per-case development test covers all planned
host rates, three cutoff positions, three resonance positions, two drive settings
and nine stimuli including a combined-control modulation case. Both identical
renders remain bit-exact and free of unconverged samples; the untested cross-product
and production quality paths still prevent a zero-incidence claim.
Development-only tests now also pass the repository's real Off/2x/4x IIR/FIR
upsampling and downsampling paths on short combined-modulation and alternating
overload probes at all planned host rates. These do not yet establish complete
quality-mode fallback freedom, alias rejection or a production-path CPU budget.
An additional 512-host-sample development check of 8x/16x FIR at all five
planned host rates passes finite, bit-exact 1- versus 127-sample-block
rendering for one combined-control modulation and +24 dB sample-alternating
and 64-host-sample plateau inputs, with no solver fallback. It is not long-run or
complete-processor evidence and does not approve these quality paths.
The plateau test verifies the actual upsampled input reaches more than
3.0 peak at the ladder input for each rate, factor and tested block size.
The short modulated render does not cover full control cycles at every
rate; it does not close the modulation-artifact gate.

After the iteration bound, the current candidate uses the last bounded iterate and
records an unconverged sample. A non-finite output clears all state and returns zero.
This last-iterate behavior is deterministic and bounded, but it is not yet accepted as
the production fallback: validation must show that it is either unreachable in the
supported matrix or replace it with a documented continuous fallback.

## Intentional simplifications and exclusions

The candidate does not currently model:

- transistor-pair device equations, thermal voltage or temperature;
- component tolerances, loading asymmetry, supply rails or device noise;
- frequency-dependent feedback-network components;
- nonlinear mixer or VCA stages;
- hardware-specific input, interstage or output impedances; or
- a hardware-accuracy claim.

Classic oscillator-3/noise modulation, final articulation, output feedback and
mixer/VCA nonlinearity remain later product-contract decisions rather than properties
of these ladder equations. Drive and Q compensation are approved only as separate,
optional post-ladder features.

## Evaluation gates

- The analytical linearized response agrees with low-level candidate measurements in
  gain and phase across the supported sample-rate and cutoff matrix.
- Absolute solver tolerances are evaluated against deeply attenuated outputs; current
  development measurements show that stopband relative error can become visible before
  any non-finite or unconverged sample is reported.
- A double-precision, progressively converged nonlinear offline reference agrees over
  low through overloaded levels and static through modulated controls.
- The offline reference preserves the host-rate prewarped continuous pole, linearly
  interpolates host input and control endpoints, uses double-precision state and bounded
  Newton solves, and verifies convergence by increasing the internal substep factor.

For `N` internal substeps, the reference uses the exact TPT coefficient for the same
host-rate prewarped pole:

```text
gHost = tan(pi cutoffHz / fs)
wc = 2 fs gHost
gReference = wc / (2 N fs) = gHost / N
```

Reference convergence is checked at low, nonlinear and overloaded levels and while
cutoff and resonance are modulated at audio-rate processing resolution.
The independent development reference brackets its stage and global roots rather
than clipping iterates to the state bound. Its coefficient divides the host
prewarped pole by the substep factor; applying `tan` a second time would change
that pole and can even produce negative gain at the cutoff ceiling. A one-substep
reference check against the candidate guards the coefficient identity. A 16-substep
overload regression passes at the ceiling across all planned host rates; this is
not yet a full high-cutoff reference-convergence claim.
At 48 kHz and the cutoff ceiling under a peak-4, +12 dB driven tone, the
16/32/64-substep solvers all converge individually, but the last-half output
differences are approximately 0.01109 RMS for 16-to-32 and 0.002689 RMS for
32-to-64. Further 64-to-128, 128-to-256 and 256-to-512 differences are about
0.000672, 0.000168 and 0.000042 RMS. The progression is consistent with
convergence but no factor is accepted as overloaded ceiling ground truth
without a defined error budget across the supported matrix.
- The nominal resonance threshold and finite-amplitude self-oscillation behavior are
  measured rather than inferred solely from the linearized equations.
- Every supported processing mode remains finite, deterministic, bounded and free of
  processing-thread allocation.
- Solver fallback behavior and tolerances are frozen before this ADR becomes Accepted.
- Product scope and the complete gain structure are approved separately.

## Acceptance status

The pre-alpha engine replacement is implemented, not validated for release.
The ladder boundary and required cutoff/resonance/drive modulation scope are frozen.
The ADR remains Proposed until the documented host-rate, cutoff, resonance, level,
drive, block-size, quality and stimulus matrices pass; self-oscillation and alias/IMD
limits are frozen; the stopband tolerance policy is resolved; and fallback incidence
is proven zero or replaced by an accepted continuous policy.
The matrix requires explicit coverage of each supported path and its agreed
boundaries, not an unreviewed Cartesian product of every parameter. A named
product reviewer must approve representative cases, boundary combinations,
operating limits and any exclusions before evidence is treated as acceptance.
On 26 September 2026 Thomas identified himself as the product owner and single
developer for this decision. The review sheet in
`docs/MONO_LADDER_ACCEPTANCE_PLAN.md` tracks approved targets separately from
candidate measurements and remaining decisions.
Thomas approved 48 kHz, 257-sample blocks and eight active voices on the
physical M1 Pro as the **first measurement-only 1x candidate-processor pilot**
on 26 September 2026. This is not an approved release operating envelope,
CPU threshold or permission to enable the candidate in production; the
remaining step 1 review-sheet decisions are pending.
On 26 September 2026 Thomas set the *future 1x candidate's minimum M1 Pro
real-time test envelope* to all four combinations of 44.1/48 kHz, eight
active voices and 128/257-sample blocks. Other exposed rate/block/voice
combinations must be measured and explicitly limited before release; this
does not remove them. The higher-mode envelopes, candidate measurements
and final sign-off remain pending. The earlier 48 kHz/257/8
pilot remains only the first measurement.
On 26 September 2026 Thomas approved the 1x *target pass rule* for each of
those four combinations: a 30-second complete-candidate Release run, zero
processing-thread allocations and solver fallbacks, p99.9 below 75% of the
block deadline, and zero simulated deadline exceedances. Report maximum
callback time and processor latency separately. This is not a measured pass,
an approved higher-mode budget or proof of actual audio-device deadlines.
On 27 September 2026 Thomas approved separating the **unchanged M1 Pro 1x
baseline target** from hardware-dependent maximum-setting envelopes. Higher
polyphony and retained 2x/4x/8x playback may have narrower M1 Pro operating
envelopes or require specified faster hardware; no faster machine or
higher-mode real-time combination is qualified by this policy. Record the
rate, block size, voice count, quality, hardware and timing rule for each
claimed tier, then require repeatable complete-candidate Release measurements
and live-host/device tests before advertising support. Retained settings still
require finite output, bounded work, processing-thread allocation safety,
approved fallback behavior, latency, spectral and listening review regardless
of hardware tier. Investigate variable M1 Pro callback tails under controlled
scheduling before revising the baseline; any change to its four combinations
or 75%-of-deadline/zero-exceedance rule requires a separate explicit product
decision. Presumed faster-CPU scaling is not qualification.
On 26 September 2026 Thomas approved retaining Mono's convincing classic-style
dry bass/lead sound target with controlled overload and smooth resonance,
without a claimed match to a particular Moog or Mother-32. Hardware and SPICE
comparisons remain diagnostic. This is approval of the *product target*, not
of this candidate's sound, numerical limits, or listening results.
**Historical offline-16x policy, superseded 27 September 2026:** Thomas
subsequently superseded the earlier 16x removal: the *planned nonlinear
candidate* has distinct Playback Quality (up to 8x) and Offline Render Quality
(up to 16x); 16x must never run in real-time playback. ADR 0001 records these
separate controls. The current legacy engine still allows 16x in playback;
neither the split nor its offline eligibility and safe mode-transition policy
is implemented. The candidate's
offline 16x spectral, stability, latency and rendering-safety gates remain
open. This product-scope choice does not approve any quality or integration.
On 26 September 2026 Thomas approved the candidate's offline-status fail-safe:
use Playback Quality when the host reports real-time processing or does not
explicitly report offline processing; use Offline Render Quality only with
explicit host offline status. Defer effective-quality switches until a safe
boundary and report the effective quality. This rule does not establish what
the safe boundary or latency transition is, does not enforce itself in the
current legacy engine, and does not approve the candidate for production.
Thomas subsequently chose `prepareToPlay`/reinitialization as the only
effective-quality switching boundary; do not switch during an active session.
Thomas approved restricting the offline 16x override to hosts with verified
offline export/reinitialization behavior; do not offer 16x in unverified hosts.
Preserve a stored 16x preference without silently substituting or claiming
that it renders at 16x there; report availability and effective quality.
Thomas approved preserving the saved 16x preference and blocking offline
export in an unverified host with a visible explanation; silently rendering
at another quality is forbidden. This is a product requirement, **not** an
implemented or proven capability: JUCE's audio callback has no portable
host-export cancellation result. Validate a host-specific preflight or export
cancellation that prevents an invalid file. If a host cannot enforce it,
offline 16x is unavailable there; silence alone must not be called a block.
Thomas selected Ableton Live 12 Suite with VST3 as the first host for this
feasibility check. Live 12.4.6 and an arm64 Vekt Mono VST3 are present locally;
no host lifecycle, export-cancellation or 16x eligibility test has been run.
VST3 requires `setupProcessing` when switching between realtime/prefetch and
offline modes, but Live's behavior and the JUCE wrapper's preparation/latency
response must be measured. The host remains unqualified for offline 16x.
Inspection of the vendored JUCE VST3 wrapper shows that `setupProcessing`
updates `isNonRealtime()` but passes `CallPrepareToPlay::no`; only its
`setActive(true)` path calls `prepareToPlay`. Therefore a VST3 mode change
alone cannot select the offline quality under the approved prepare-only
boundary. The Ableton test must observe deactivation/reactivation and
latency negotiation as well as mode reporting. This has not been tested
in Ableton; export cancellation is still unproven.
For an exceptional offline-to-real-time status change without reinitialization
while 16x is active, the approved last-resort candidate guard checks status
on every callback, stops processing at 16x, returns silence, latches an
off-audio-thread diagnostic and requires reinitialization before audio
resumes. Do not switch DSP state or latency inside `processBlock`. This is
not seamless switching or an implemented feature. Qualify host behavior,
latency, saved-setting handling and audible interruption before accepting
offline 16x.
On 26 September 2026 Thomas approved the Offline Render Quality default:
follow Playback Quality (initially 1x in new sessions) unless an explicit
offline override is set. While following, playback-quality changes affect
the requested offline quality; a saved override has its own value and can
be cleared back to follow. This is an unimplemented product contract, not
evidence of safe switching or latency negotiation.
On 26 September 2026 Thomas approved the **1x artifact review method** in
`docs/MONO_LADDER_ACCEPTANCE_PLAN.md`: compare same-input, raw-tap candidate
and converged, band-limited reference renders across planned rates, drive,
resonance and modulation; attribute the largest excess host-band components,
then document unblinded listening checks with a named listener on
representative patches and settings. The 27 September 2026 revision replaces
the previously approved blinded protocol, not the numerical review method.
The 1x numerical ceiling and control-specific sound checks remain pending;
Thomas's positive 1x preference does not resolve them. The narrow filtered
48 kHz probe is not an acceptance pass.
Thomas, product owner and single developer, is the named listening reviewer.
Compare the 1x candidate, legacy and converged reference where useful, then
check retained 2x/4x/8x on intended cases.
Record patch, quality, rate, controls, monitoring conditions, audible artifacts,
gain differences and preference. Keep original renders for gain/headroom review;
level and latency matching may help diagnose uncertain differences but are not
mandatory listening gates. No higher-mode listening result has been recorded.

### Current validation sequence to leave Proposed (supersedes the historical cutover sequence below)

1. Exercise the ordinary coupled Mono processor at each retained quality in
   Debug and normal Release VST3/Standalone builds. Verify finite output,
   deterministic recall, preset-tail isolation, reset/silence, quality deferral
   and reported latency; do not infer measured audio delay from that latency.
2. Document control-specific listening (including factory presets, gain and
   headroom), reference/spectral/IMD and modulation results, resonance/startup,
   bounded work and fallback incidence. Freeze exceptions and limits with Thomas.
3. Measure the complete normal Release processor under controlled scheduling,
   and test actual host/device callbacks, allocation and timing for every claimed
   rate/block/voice/quality envelope. Distinguish simulated overruns from
   device glitches. Test preset/project recall, older normalized snapshots,
   quality/latency negotiation and audible transitions in real hosts.
4. Record source/build IDs, evidence and remaining limitations in
   `docs/MONO_VALIDATION.md`. Only accept this ADR and claim release readiness
   after all retained paths and the final quality-control contract pass, or
   explicitly revise ADR 0001/0005. Do not reinstate legacy by default.

### Historical staged cutover proposal (27 September 2026; superseded 28 September 2026)

Track the current step statuses, evidence and next action in
`docs/MONO_LADDER_ACCEPTANCE_PLAN.md`. Update that live plan when evidence
or a product decision changes; this ADR remains the authoritative decision.

1. Approve or revise the product contract: name the reviewer; record the 1x
   audible/alias policy, overload and modulation limits, stopband tolerance,
   resonance/startup limits, solver fallback policy, latency and the supported
   rate/block/voice envelope on physical target hardware. The numerical targets
   in `docs/MONO_VALIDATION.md` are proposals until signed off. Retained
   2x/4x/8x choices still require per-path evidence under explicitly
   approved conditions. Selectability alone guarantees no real-time performance
   at every rate, block size and voice count.
2. Establish coupled 1x sound feasibility before replacing the production filter:
   compare the raw one-step coupled path, nested comparator and at most two
   implementable alternatives against an adequately band-limited,
   progressively converged higher-rate reference.
   Test absolute-frequency, resonance, level/overload and modulation cases;
   align latency and distinguish intended harmonics/sidebands from artifacts.
   Document unblinded listening on representative patches and settings early. An
   unfiltered reference return or a passing coherent-bin regression cannot
   decide audibility or alias acceptance. Stop and revisit the model/product
   target if no 1x sound candidate merits complete-path measurement.
3. For a viable coupled candidate, use the measurement-only selection to exercise
   the real Mono voice and processor path without enabling it in production.
   Verify control/gain mapping, state/reset, processing-thread allocation,
   bounded work and latency. Compare candidate and legacy in Release on the
   physical M1 Pro over the approved rate/block/voice cases, including driven
   modulation and long-duration callback tails. Filter-only timing and the
   complete *legacy* processor benchmark cannot pass this gate. Offline
   simulated deadlines must not be reported as actual device callback misses.
4. Record a distinct coupled 1x default-path go/no-go result, then qualify each
   retained higher mode over its approved spectral, stability, latency and
   complete-processor CPU envelope. If a 1x-only or mixed legacy/candidate
   release is desired, explicitly revise ADR 0001 and this ADR, including
   quality-switch behavior; it is not covered by the present acceptance scope.
5. Close the analytical and converged nonlinear-reference matrix, IMD,
   self-oscillation, modulation, stopband, fallback and long-run safety gates.
   Record documented listening checks, numerical/CPU limit approval, measured
   exceptions and the named reviewer's sign-off here. Only then change this
   ADR to Accepted and consider production integration. If coupled does not
   meet the approved contract, retain legacy production and record rejection,
   revision or supersession instead of treating development tests as a pass.
6. Exercise and qualify coupled on every retained quality path in development
   before acceptance. After acceptance, switch and validate the host entry
   point without development flags, including playback/offline follow and
   override once implemented, recall (including older normalized host
   snapshots), effective latency, gain/headroom, live-device behavior and full
   production-format regressions. Keep legacy
   temporarily as a comparison fixture; once the replacement has passed,
   remove legacy DSP and obsolete development engine selectors in a separately
   tested cleanup. Record cutover and deletion decisions and build identifiers.

**Cutover/rollback criteria (27 September 2026; not sign-off):** Thomas, product
owner and sole developer, must record a dated go/no-go, exact source/build IDs,
supported hardware and rate/block/voice envelopes, per-quality sound and
reference/spectral/stability/latency results, complete-processor Release CPU and
allocation evidence, live host/device checks and explicit exceptions before
production integration is allowed. The 1x four-combination M1 Pro timing rule
is unchanged; distinct 2x/4x/8x rules require approval before scoring. If a
retained mode does not qualify, leave legacy in production and revise ADR 0001
and this ADR before changing scope; do not silently retain a mixed engine.
Keep a known-good legacy source/build available through host regressions. A
non-finite sample, unapproved fallback/allocation, failure of an approved CPU
or host envelope, incorrect recall/latency, or rejected sound blocks promotion:
restore legacy in a separately tested build and record the regression and
rollback revision. Do not attempt a mid-session engine substitution. After a
verified coupled production build, legacy deletion requires a second recorded
decision and full production regressions; restore and revalidate the archived
legacy revision if rollback is needed after cleanup. Detailed staged checkpoints
and the per-quality evidence audit live in
`docs/MONO_LADDER_ACCEPTANCE_PLAN.md`.

**Historical 27 September gate (superseded by the 28 September pre-alpha
implementation decision):** Measurement-only integration for step 3 could
precede acceptance; switching Mono's ordinary render path could not. Legacy
remained active and `production_integration_allowed` stayed false. The former
C++ selector was pre-acceptance development integration, not a sound, CPU or
host qualification. The current ordinary render path is coupled; **release
validation remains open**.

### Development evidence and remaining limitations (dated results are historical)

Two-second development impulse tests at the 10 Hz floor show that the proposed
1 kHz tail-amplitude targets do not transfer to the floor: resonance 0.98 can
remain above `1e-5` peak, and resonance 1 can remain below `1e-4` peak/RMS
at high host rates. Floor-specific duration and amplitude limits need review.
An Off-path 30-second impulse regression at all five host rates now meets
*proposed*, separate 10 Hz floor targets using 5–10 s and 25–30 s windows:
resonance 0.98 late peak below `2e-6`, RMS below `1.5e-6` and peak less
than 20% of the early-window peak; resonance 1.0 late peak above `3e-5`,
RMS above `2e-5`, at least 80% of its early RMS, and late crossing frequency
within 0.5% of 10 Hz. These targets are not approved; solver-tolerance effects,
noise-seeded startup and full-spectrum purity remain open. A separate 48 kHz
30-second development regression now measures 2x/4x IIR/FIR paths: the
resonance-0.98 impulse decays while resonance 1.0 retains a roughly 10 Hz
tail, with no solver fallback; exact digital silence remains silent after
reset. These observations are not approved quality-path limits or a
noise-seeded startup test (see `docs/MONO_VALIDATION.md`).
A separate deterministic 48 kHz, two-second noise-seeded 1 kHz probe
through Off/2x/4x shows late-window growth at resonance 1.0 without
solver fallback. It does not establish a 10 Hz onset or an approved
startup limit. Full spectral coverage, documented listening checks, product
approval and the candidate's complete production-path Release budget
remain outstanding.
An expanded coherent alias audit exposes a high-drive review item:
at 48 kHz, +24 dB drive and resonance 0.98, folded seventh is about
`-51..-52 dBc` at 2x and `-75..-76 dBc` at 4x, although the folded-fifth
regression passes. These values are not a full-spectrum limit; do not
approve the candidate using the fifth alone or extrapolate the +12 dB
alias proposal to overload.
A broader 48 kHz host-band coherent-bin audit at 7 kHz input also finds
an unattributed 5 kHz spur near `-48.6 dBc` through 2x at +12 dB,
resonance 0.98, and stronger spurs under +24 dB overload. Their origin
requires a latency-aligned high-rate reference comparison; they must
not be called aliases or passed against the alias limit without that
comparison. The proposed spectral acceptance gate is unresolved.
A targeted 16/32/64/128-substep offline-model probe places that 5 kHz
component near `-88 dBc`, about 39 dB below the observed 2x bin.
The reference bin changes by about `0.023 dB` from 64 to 128 steps;
reference-render convergence and the complete spectral matrix are
still open; the 2x quality path cannot be signed off using the
folded-fifth-only pass.
An internal-tap check confirms the problematic 2x 5 kHz component is
present before downsampling (`-48.59 dBc`), while the upsampled input
contains negligible 5 kHz energy. A 4x path has about `-106.19 dBc`
at its 3 kHz internal spur. The factor-dependent pattern supports an
internal nonlinear aliasing hypothesis, not a signed-off mechanism or
quality policy; 8x/16x FIR improve this one coherent spectrum but
require complete CPU, latency, matrix and listening review.
The 2x bin is consistent with the 91 kHz thirteenth harmonic folding
to 5 kHz at 96 kHz internal rate; the 4x bin is consistent with the
189 kHz twenty-seventh harmonic folding to 3 kHz at 192 kHz.
Neither harmonic was independently measured without aliasing.
In a separate coherent internal-tap comparison for the same 48 kHz
stimulus, the 2x FIR folded 5 kHz component measures `-48.59 dBc`,
whereas a 16x FIR render measures the unfolded 91 kHz component at
`-139.14 dBc`. These are different discretizations and input filters:
the latter cannot be substituted for the unaliased output of the 2x
solver. The arithmetic folding hypothesis is not a demonstrated
mechanism or resolution of the 2x spectral gate.

A same-upsampled-input development probe feeds each 2x IIR and FIR input
to the candidate and an independent double reference at 96 kHz. Both
filters give about `-48.59 dBc` at the internal 5 kHz bin for the candidate
and one-step reference; the 4/16/32-substep reference measures about
`-78.86`, `-80.36` and `-80.43 dBc` respectively. This identifies the
one-step 2x discretization as a substantial contributor for this stimulus,
not a sign-off for an
unimplemented substepped production path. Reference-render convergence,
complete spectral coverage and measured real-time cost remain open.

A separate host-tap check after the actual 2x IIR/FIR downsampling finds
the same `-48.59 dBc` candidate 5 kHz bin and approximately `-78.86`
and `-80.43 dBc` with the four- and 32-substep offline references.
The single-stimulus improvement reaches the host output, but no
substepped candidate is integrated or approved for production.
A coherent 1–23 kHz host-band scan on that 48 kHz tone confirms 5 kHz
remains the largest non-harmonic bin at +12 and +24 dB with either 2x
filter. At +24 dB it measures about `-24.27 dBc` for one step,
`-59.28 dBc` for four offline steps, and `-60.09 dBc` for 16 steps.
This single stimulus is not a full-band overload specification or a
validated real-time quality policy.
A fixed-7 kHz/+12 dB, resonance-0.98 2x IIR diagnostic scans every
100 Hz coherent host bin below Nyquist across 44.1/48/88.2/96/192 kHz.
The largest non-direct-harmonic bin improves by roughly 29–32 dB with
four offline substeps at 44.1/48/88.2 kHz, but at 96 kHz the 47 kHz
maximum **rises** from `-95.61` to `-93.24 dBc`. At 192 kHz the
maxima are `-133.69` and `-152.57 dBc`, at different frequencies.

At the 96 kHz 47 kHz bin, 16/32 offline substeps give approximately
`-93.11/-93.09 dBc`: the four-step rise is not just a four-step outlier,
but single-bin convergence does not establish a converged full render.
The 192 kHz pre-downsampling tap measures 49 kHz at `-85.10/-82.73`
dBc for candidate/four steps, while its 47 kHz bin is below `-155 dBc`;
the 96 kHz host 47 kHz bin follows the 49 kHz change. This supports
a measured 49 kHz component folding through 2x downsampling, not a
proven explanation of its substep-dependent generation.

Thus the improvement is not uniform across rates/bins. A separate
five-rate normalized-frequency scan shows improvement with four offline
substeps at resonance 0.5/0.98 and +12/+24 dB; its scaled frequencies
cannot substitute for independent absolute-frequency coverage. See
`docs/MONO_VALIDATION.md` for the measured maxima and scope.

The product permits investigating replacement of 2x with a better-
validated candidate quality path; it has **not** approved any replacement
or the ladder. A normalized five-host-rate coherent-bin probe gives
promising 8x/16x FIR *spectral* results for its single stimulus, but
the cost and remaining validation gates still apply.
The product additionally permits a 1x default on Apple M1 Pro when 2x
cannot meet the real-time budget, while requiring 2x, 4x, 8x and 16x
to remain selectable in Mono's GUI. This is permission to expose and
evaluate the **legacy** quality paths, not a waiver of spectral/CPU gates
for a candidate nonlinear ladder at any selectable factor. Measured
complete-path cost and reviewer-approved operating conditions remain open.
An isolated Release candidate-path cost probe on Apple M1 Pro measures
8/16 candidate ladders and one mono oversampling bank, not the complete
Mono processor. At 48 kHz, 257-sample blocks and 8 filters, the 8x FIR
median of about `6046 us` exceeds the simulated `5354 us` callback
deadline (93 of 94 blocks exceed it); 16x FIR is slower. At 96 kHz,
even 4x FIR with 8 filters costs about `3017 us` median against a
`2677 us` simulated deadline. These half-second offline timings are
not real callback-miss counts or an approved CPU limit, but rule out
claiming that the current unoptimized 8x/16x replacement meets the
proposed performance gate on this hardware. Full results and caveats
are in `docs/MONO_VALIDATION.md`. The earlier proposed CPU target was a
*hypothetical* Apple M5 Pro, assumed to offer 2x M1 Pro single-core
speed and ten usable cores. Neither those assumptions nor callback
tails have been measured on such hardware. The product subsequently
selected the local physical M1 Pro as the CPU measurement target while
retaining all quality and polyphony options for more capable hardware.
A new Release tool times the complete **legacy** Mono processor with eight
voices: a short 48 kHz, 257-sample diagnostic reports median callback
times near 1.03/1.97/3.82/7.49/14.50 ms for 1x/2x/4x/8x/16x respectively,
after a consistent 100 ms audio-time warm-up, against a simulated
5.35 ms deadline. This neither benchmarks the
unintegrated candidate nor approves operating limits. The remaining
30-second matrix, candidate integration for testing, spectral review and
listening remain required. ADR acceptance remains blocked.
A 30-second *legacy* 1x, 48 kHz, 257-sample, eight-voice run measured
`1044.50 µs` median, `2681.04 µs` 99.9th percentile and `4746.67 µs`
maximum over 5,604 offline callbacks with no simulated deadline
exceedances. This is one baseline configuration, not evidence that the
candidate meets the complete-path CPU gate.
A separate 30-second legacy 1x run at the same rate and block size with
16 active voices measured `2140.96 µs` median, `2478.50 µs` 99.9th
percentile and `4625.38 µs` maximum across 5,604 callbacks, with zero
simulated deadline exceedances. Neither run exercises the candidate.
The final development test binary, including the 8x/16x plateau and
upsampled input-level checks, passes 243 cases and 6,627,140 assertions
(exit 0). This verifies the test run, not the spectral, complete candidate-
processor CPU, listening or product approval gates. Use the bounded
feasibility/stop-go workflow in `docs/MONO_VALIDATION.md` before expanding
the full matrix. It changes the order of evaluation, not ADR 0001's
selectable paths or this ADR's Proposed status.
An experimental persistent-worker, filter-only 4/10-lane Release
probe reduces median cost on larger blocks but is not a production
real-time renderer: it uses blocking waits, and 1/16-sample tail
times remain variable and can exceed deadlines even on the M1 Pro.
It does not make hypothetical 10-core/M5 Pro scaling an accepted
CPU budget. See `docs/MONO_VALIDATION.md` for protocol and results.
Proposed, explicitly unapproved numerical review targets and the revised unblinded
listening checks are recorded under "Proposed Acceptance Package" in `docs/MONO_VALIDATION.md`.
Neither that proposal nor the passing development-reference regression grants
production integration. Product approval of targets, measured results, documented
listening and a production-path Release CPU budget is still required.

## References

- Antti Huovilainen, *Non-Linear Digital Implementation of the Moog Ladder Filter*,
  Proceedings of DAFx-04, 2004.
- Vadim Zavalishin, *The Art of VA Filter Design*, Native Instruments.
