# ADR 0001: Oversampling Quality

## Status

Accepted

## Decision

The 3 October 2026 decision below (uniform quality choices) sets the current
choices and defaults; this original decision is kept as history.

Nonlinear processors may offer Off, 2x, and 4x oversampling. Both JUCE
maximum-quality polyphase IIR and equiripple FIR paths are prepared in advance.
The product labels these `Minimum Phase` and `Linear Phase` rather than ranking
one as universally better.

The default is 4x minimum phase. It provides strong alias rejection without the
latency and pre-ringing of the FIR alternative. Linear phase remains available
when phase response is the user's priority. Off adds no oversampling latency.

Factors above 4x are excluded unless measurements show materially lower audible
aliasing and documented listening checks demonstrate a useful benefit on supported
hardware. Blinded comparisons are optional if the sound decision is uncertain.

### Uniform quality choices (3 October 2026)

**Current decision, Thomas, 3 October 2026:** every product offers the same
non-automatable **Tracking Oversampling** (real-time) and **Offline
Oversampling** (used only when the host reports offline processing) choices,
defined once in `vekt/dsp/OversamplingChoices.h`: Off, 2x/4x minimum-phase IIR
and 2x/4x/8x/16x linear-phase FIR. This includes 16x for Mono, in real time
too, reversing the 27 September Mono scope below; 16x with many voices is
CPU-heavy (see the Release screen in MONO_VALIDATION.md: it needs Multicore and
few voices in real time), and Mono's editor says so. Mono's single `quality` parameter is
replaced by the two shared parameters (pre-release; its frozen parameter and
state fixtures were replaced). Defaults stay per product: Rav and Glimmer track
at 4x IIR and render offline at 16x FIR; Mono tracks Off and renders offline at
4x FIR. Mono's real-time default sound is unchanged, but a default offline
render (bounce) now uses 4x FIR where the single control rendered at 1x, so
bounces differ from playback and report the FIR latency. The "factors above 4x are excluded"
rule above no longer describes the products, which have offered 8x and 16x.

### Instant quality changes (2 October 2026)

**Current decision, Thomas, 2 October 2026 (ADR 0010):** a quality change
applies at the start of the next audio block in every product, during playback
too; the audio may drop or click, and no smooth transition is required. This
supersedes every rule below that defers a change until the transport stops, a
"safe boundary", or sustain and release tails finish. Mono already switched at
once; Rav and Glimmer stopped deferring on this date.

### Mono pre-alpha quality scope (26 September 2026)

**Mono decision of 27 September 2026 (superseded on 3 October 2026; it superseded the offline-16x
proposal below):** Remove 16x from both planned and selectable Mono quality.
Keep 1x/2x/4x/8x (indices 0–3) in the existing single legacy control and in
the candidate's planned Playback Quality and Offline Render Quality. Offline
Render Quality still follows playback unless explicitly overridden, but its
highest override is 8x. No offline-16x host eligibility, export-blocking or
16x listening gate is required for the retained product modes. This is a
product scope/CPU concern, **not** a measured conclusion from short diagnostic
cost probes and not acceptance of the remaining modes. Existing project state
with stored quality index 4 (16x) is rejected as a whole during recall, rather
than silently clamped to 8x; indices 0–3 keep their meanings. Hosts cannot be
notified of this refusal through JUCE's void `setStateInformation` API: users
must check older sessions before relying on recall. Earlier decisions and
measurements below are retained as historical context, not active policy.
The four-choice range also changes normalized host values: old normalized
`1.0` meant 16x, but now means 8x. Serialized choice indices 0–3 retain
their meanings; host-managed normalized parameter snapshots cannot be
intercepted by project-state validation. Check older host sessions explicitly.

**Historical decision (superseded 27 September 2026); revised candidate scope,
26 September 2026:** Thomas, product owner,
superseded the earlier removal of 16x. The planned nonlinear candidate has
separate, non-automatable **Playback Quality** (1x/2x/4x/8x, default 1x)
and **Offline Render Quality** (follow Playback Quality by default; an explicit
override may select 1x/2x/4x/8x/16x) settings. The latter applies
only when offline processing is verified; **16x must never run in real-time
playback**. The 2x path uses minimum-phase IIR and 4x/8x/16x use linear-phase
FIR unless separately revised. Offline 16x requires
offline spectral, stability, latency, deterministic rendering and bounded-work
validation; do not claim an audible advantage without documented listening
observations for the relevant quality setting. Blinding is not a release gate.
The other retained modes require per-path spectral, stability, latency and
complete-processor CPU review under explicitly approved operating conditions.
Short M1 Pro cost probes do not justify real-time 16x, even on large blocks.

**Interim implementation, unchanged and not offline-gated:** the current
*legacy* Mono engine and GUI still expose a single 1x/2x/4x/8x/16x setting,
with 1x default. Parameter indices
0/1 retain 1x/2x; indices 2–4 currently add 4x/8x/16x. Legacy 2x is IIR,
4x/8x/16x are FIR, and quality changes remain non-automatable and deferred
until transport stops and voices/sustain finish (superseded: changes apply at
the next block, 2 October 2026). This interim availability
does not implement an offline-only restriction or integrate the candidate.
The old normalized host value `1.0` can currently select 16x instead of the
former two-choice "High" (2x). Mono has no pre-alpha compatibility promise;
do not claim old sessions are transparently migrated.

**Offline status rule approved by Thomas, 26 September 2026:** use Playback
Quality whenever the host does not explicitly report offline processing or
reports real-time processing. Use Offline Render Quality only when the host
explicitly reports offline processing. Defer changes between effective
qualities until a safe boundary and report the effective quality. Never
activate 16x in real-time playback. **Offline default approved by Thomas,
26 September 2026:** new sessions follow Playback Quality (initially 1x)
until the user explicitly selects an independent offline override. While
following, subsequent Playback Quality changes also change the requested
offline quality; an explicit override must be independently recalled and
can be cleared to return to following. Before candidate release, define
the latency protocol and test hosts that omit or change offline status
mid-session. **Boundary approved by Thomas, 26 September 2026:** select
or change effective quality only at `prepareToPlay`/reinitialization, not
during an active processing session. **Host-verified offline 16x policy approved
by Thomas, 26 September 2026:** offer the 16x offline override only in hosts
whose offline export and reinitialization lifecycle has been tested for this
rule. In unverified hosts do not offer 16x (cap available offline override
choices at 8x); do not silently remap a stored 16x override or claim that
such a project will render at 16x there. Report the unavailable selection
and effective quality explicitly. **Saved-override policy approved by Thomas,
26 September 2026:** if an unverified host recalls a 16x offline override,
preserve the preference and require a visible explanation and blocked export;
never silently render at a lower factor or represent that file as 16x. JUCE's
`processBlock` does not provide a portable host-export cancellation result.
Before claiming this policy works, demonstrate an enforceable host-specific
preflight/cancellation mechanism that prevents creation of an invalid export.
If no such mechanism exists, do not advertise or enable 16x in that host;
silence and a warning alone do not constitute blocked export. As a last-resort
invariant guard, check offline eligibility at every processing callback after
preparation without changing quality or latency there. If 16x is active and
the host no longer
explicitly reports offline processing, do not process at 16x: return silence
for that callback and subsequent callbacks, latch a diagnostic for reporting
outside the audio thread, and require reinitialization before resuming. This
may interrupt sound and is not an acceptable ordinary quality transition.
Validate the guard and host eligibility before offering 16x; if a host fails
the lifecycle tests, do not offer 16x there. Do not assume transport-stop
status proves offline rendering.
The present `isNonRealtime()` check only selects which quality a change
applies (tracking or offline); it does not guard rendering.
Separate control identifiers/mappings require deliberate state/schema and
preset updates; existing single-quality values must not silently become the
offline override. Validate recall of the follow/override state and its value,
explicit effective-quality reporting, latency negotiation, note/tail and
transport transitions, and preallocation. No callback-time resets,
allocations or silent quality substitution are approved. The status-based
quality selection, reinitialization boundary and exceptional fail-closed
path are approved
product rules. Host-specific eligibility, saved-override handling, guard
behavior, audible interruption and latency negotiation remain unimplemented
and unverified.
Do not change the existing legacy renderer as a side effect of this decision.
Off must be characterized for aliasing rather than represented as equivalent
to oversampled modes. Selectable higher factors are not a guarantee of
glitch-free operation at every rate, block size and voice count.

## Consequences

- Quality paths consume memory because they are prepared before processing.
- Quality changes alter latency and are non-automatable project settings.
- Changes apply at the next audio block, during playback too, and may drop audio
  (2 October 2026; previously deferred during confirmed playback).
- Dry signals must be delayed by the active path's exact integer latency.
- Every quality path requires frequency, aliasing, latency, and stability tests.
