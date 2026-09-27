# Mono Ladder Acceptance Plan

**Updated:** 27 September 2026

**Decision:** ADR 0005 is Proposed; the legacy engine remains in production.
**Replacement direction (approved 27 September 2026):** Qualify the coupled
solver as the preferred candidate and retire the legacy render engine as soon
as the complete replacement is accepted and integrated safely. This is not
permission to switch the host entry point or remove legacy today.
**Cutover objective (27 September 2026):** Deliver one coupled production engine
for every retained quality, validate the production host path, then remove
legacy in a separately verified change. Prepare production-equivalent tests
before cutover, but keep `production_integration_allowed=false` until Thomas
signs the acceptance decision. A 1x-first *qualification order* is not approval
to ship a 1x-only or mixed-engine plugin.

**Current gate:** Provisional 1x candidate integration into Mono's development
processor passes focused silence, reset, finite/bounded, determinism and
quality-boundary smoke tests. An opt-in coupled solver agrees with the nested
solver on focused comparisons and improves the measured 30-second pilot CPU
tail; it is not the default. Repeated offline timings vary: a new 44.1 kHz/257
run had a simulated deadline exceedance after earlier passing runs. The
44.1 kHz/128 coupled path failed zero-exceedance checks in further runs both
with and without per-callback diagnostic snapshots; 48 kHz/128 failed twice
and passed once. Legacy also exceeded deadlines
in the matched 44.1 kHz run. No cause or real-time safety is proven.
Two unblinded render pairs have been generated; Thomas reports the coupled 1x
development audition sounds much better than legacy and favors legacy removal.
Control-specific listening checks and allocation safety remain open. The nested solver missed the
48 kHz pilot CPU target.
Acceptance still requires numerical spectral limits, latency and higher-mode
operating criteria. The positive unblinded 1x preference is recorded, but
representative listening checks across controls and retained modes remain open.
The 27 September Mono scope revision removes 16x from selectable and planned
quality. Earlier offline-16x host/export-blocking gates below are historical,
not requirements for the retained 1x/2x/4x/8x paths. This is a product decision
motivated by CPU concern, not a measured CPU conclusion from the short probes.
Stored 16x project states are rejected in full rather than silently recalled at
8x; users should check older projects because the host state API cannot report
the rejection. Remaining quality and host gates still apply.
Step 2 reference engineering is in progress: a 48 kHz, two-bin filtered
probe is complete, but other rates, frequencies, modulation and reference
error budgets remain open.

**Next action:** Follow up Thomas's positive coupled 1x development audition
with documented unblinded checks on representative patches/settings for gain/headroom,
resonance, overload and modulation; the preference does not verify those
behaviors. Investigate callback-tail spikes
at both 128 and 257 samples
with an actually controlled, documented host scheduling protocol; a default-
scheduler four-configuration repetition and a later sequential CPU-clock pair
below still show variable tails.
An opt-in thread-CPU diagnostic now distinguishes elapsed wall time from
calling-thread CPU time in coupled cost runs; preliminary overruns are
consistent with time off-CPU, but their cause and live-host impact are unknown.
Prioritize coupled-path sound, safety and complete-processor qualification
over further nested-solver optimization; keep nested as a diagnostic reference.
The self-verified C++ `new` probe reported
zero covered calling-thread `new` calls in seven 30-second runs, but does not cover
direct `malloc` or other threads. Do not promote the solver on median speed or
isolated passing runs. Thomas still needs to audition the standalone
preview at a safe monitoring level; engine-labeled, unequal-level WAVs alone
do not establish gain/headroom safety. The broader acceptance matrix remains open.

## Provisional integration track (pre-alpha, not ADR acceptance)

- **Complete for provisional wiring — 1x voice/processor:** The candidate
  solver now lives in `VektMonoCore`. An explicitly enabled local C++
  development processor uses it only at 1x; normal host-created instances and every higher factor still
  use legacy. The current single quality control is unchanged. The focused
  processor smoke tests passed 34,464 assertions in two cases before the
  later render/UI tests, including bit-identical 16x development/legacy
  output and three-rate exact silence/reset/overload checks. This does not
  establish the final sound or all-control modulation.
- **In progress — minimum smoke:** The explicit development Debug test and
  ordinary Audio Lab Release tools build. A local C++ candidate-enabled
  Release cost tool builds separately. The ordinary Release VST3 builds with
  the development option OFF. A standalone-only `Vekt Mono Ladder Preview`
  builds in the development configuration with bundle ID
  `com.vekt.mono.ladderpreview`; its entry point explicitly selects the
  candidate at 1x. The ordinary `Vekt Mono` entry point remains legacy.
  The preview editor labels its effective engine as `DEV CANDIDATE 1x`
  or `DEV PREVIEW: LEGACY` at higher factors. The isolated editor test
  passes without JUCE assertions after replacing UTF-8 bullet prefixes
  with ASCII separators. Preview user presets use a separate directory, though the provisional
  parameter schema is still shared. The app has **not** been launched or
  listened to. Two 48 kHz legacy/candidate WAV and JSON pairs
  (filter-sweep and envelope) are under `/tmp/vekt-provisional-audition/`
  (temporary files, not durable evidence); the candidate's left peaks are
  0.537/0.352 versus legacy 0.360/0.141.
  These are engine-labeled, *unblinded* development renders, not listening
  judgments or a headroom/alias pass. Audition at safe monitoring level;
  check processing-thread allocation and fuller note/control transitions.
  The host VST3 has not been enabled for the candidate. At this earlier preview
  increment, no listening or allocation result had been recorded. The render/processor focused selection
  passes 178,490 assertions in six cases, including preview-name and
  effective-engine UI checks. The full development suite against that
  latest binary passed 252 cases and 6,878,676 assertions (exit 0).
- **Development audition wiring (27 September 2026; not production):** The
  standalone Ladder Preview now selects coupled at 1x. A separate
  `audio-lab-coupled` CMake preset builds Audio Lab with an explicit coupled
  Mono processor at 1x; the existing Audio Lab preset and normal Mono plugin
  continue using legacy. The editor identifies `DEV COUPLED 1x`, `DEV NESTED
  1x` or `DEV PREVIEW: LEGACY` so a listening report can identify the actual
  engine. The coupled preview now exercises coupled at 1x/2x/4x/8x;
  the normal plugin remains legacy. This is development wiring only,
  not qualification. The launcher is
  `scripts/run-mono-coupled-audio-lab.sh`; select Mono as Audio Lab source,
  check the editor engine label, start muted/low and arm output only after
  checking the device level. Focused development processor/UI and coupled
  comparisons pass 375,998 assertions in nine cases. Thomas subsequently
  reported coupled 1x sounds much better than legacy and favors removing
  legacy. The report does not document listening conditions or individual
  control/gain observations; it is not an accepted
  default or approval to delete the higher-quality legacy paths.
- **Complete as short diagnostics, not CPU gate:** At 48 kHz, 257 samples,
  eight voices and 1x, a candidate-enabled Release cost tool measures
  complete processor callbacks after warmup: median 2267.791 us versus
  1102.167 us legacy; 10 callbacks, zero simulated deadline exceedances
  and zero reported latency in both. Candidate output sum of squares differs
  materially (392.084 vs 82.790): gain/headroom and listening require review.
  A separate 2-second candidate-only probe at the same setting measured
  374 callbacks, median 2202.958 us, p99.9/max 4667.917 us and zero
  simulated deadline exceedances. The approved p99.9 target is less than
  75% of the 5354.167 us block deadline (4015.625 us); **this diagnostic
  exceeds that target**. Neither probe instruments allocations or replaces
  a matched 30-second candidate/legacy run. The four approved 1x CPU
  configurations remain unqualified; do not claim a real-time pass.
- **In progress — coupled solver experiment (27 September 2026):**
  `processCoupled()` solves the four existing stage equations with a sparse
  cyclic Jacobian and bounded damped Newton iterations. The nested `process()`
  remains the default; only an explicit development constructor or cost-tool
  mode selects coupling. A five-rate, 1 kHz/near-ceiling, driven and
  modulated comparison against nested and the one-step independent offline
  reference passes; a 48 kHz settled host-band comparison at +12/+24 dB also
  passes. Focused tests: 123,366 assertions in four cases (including abrupt
  seeded-input and control changes), residual at most `2e-7`, no observed
  solver fallback or non-finite samples in those cases.
  The verified 30-second sequential Release pilot (48 kHz, 257 samples,
  eight voices, 1x, 5,604 callbacks per mode) measured nested/coupled
  median `2125.167/1175.166 us`, p99.9 `4618.500/1355.958 us`, maximum
  `6164.333/1379.542 us` and simulated deadline exceedances `3/0`.
  Coupled met the approved p99.9 and zero-simulated-exceedance timing rules
  **for this measured workload only**. Output sum-of-squares was
  `251841.314/251841.370`;
  similar energy does not prove audibility or full-spectrum equivalence.
  The other three approved coupled-only 30-second runs (eight voices, 1x)
  completed with exit 0. Values below are offline simulated callback
  measurements, **not** actual audio-device deadlines:

  | Host rate | Block | Callbacks | p99.9 / 75%-deadline (us) | Max (us) | Simulated exceedances | Timing rule |
  | --- | ---: | ---: | ---: | ---: | ---: | --- |
  | 44.1 kHz | 128 | 10,336 | 1984.291 / 2176.871 | 5204.750 | 2 | Fail: exceedances |
  | 44.1 kHz | 257 | 5,148 | 1377.750 / 4370.748 | 1435.000 | 0 | Meets measured timing rules |
  | 48 kHz | 128 | 11,250 | 3639.292 / 2000.000 | 11213.958 | 19 | Fail: p99.9 and exceedances |
  | 48 kHz | 257 | 5,604 | 1355.958 / 4015.625 | 1379.542 | 0 | Meets measured timing rules |

  **Isolated repetitions (same Release workload, not a correction to the
  first measurements):** at 48 kHz/128, 11,250 callbacks yielded median
  `585.375 us`, p99.9 `735.833 us`, maximum `2474.583 us`, zero simulated
  exceedances; at 44.1 kHz/128, 10,336 callbacks yielded median `600.583 us`,
  p99.9 `2126.417 us`, maximum `3238.208 us`, four exceedances. Both runs
  completed, with zero reported solver fallback/non-finite samples. The
  slowest callbacks used `4093/2048` and `4095/2048` Newton iterations per
  ladder sample respectively, close to each run's roughly two-iteration
  average; the counters do **not** identify increased Newton work as the
  reason for the elapsed-time outliers. No processing-thread allocation
  instrumentation is present. A subsequent run used the updated cost
  harness to aggregate solver work across every over-deadline callback;
  its result is recorded below.

  A further isolated 44.1 kHz/128 run (10,336 callbacks) with work counters
  recorded p99.9 `1904.583 us`, maximum `3336.750 us` and one simulated
  exceedance. That over-deadline callback used 4096 Newton iterations and
  4096 line-search trials for 2048 ladder samples—two per sample, near the
  run average—and had zero solver fallback/non-finite samples. In a subsequent
  sequential *matched* legacy/coupled run at the same setting (exit 0),
  legacy median/p99.9/max were `521.500/1322.875/3446.000 us` with **two**
  simulated exceedances, whereas coupled measured
  `589.167/1771.709/2708.667 us` with **zero**. The coupled slowest callback
  again used two iterations per ladder sample. This establishes run-to-run
  tail variability on the offline harness; it neither proves OS scheduling
  is the cause nor erases earlier coupled failures. The repeated runs included
  diagnostic traversal *outside* the timed callback, so their timing is not
  strictly interchangeable with earlier uninstrumented runs.

  **C++ allocation-probe reruns (27 September 2026):** The Release cost
  executable now self-tests its calling-thread C++ `new`/`new[]` interception
  with a deliberate allocation before counting calls during `processBlock`.
  At 44.1 kHz/128 (10,336 callbacks) it recorded zero covered `new` calls,
  p99.9 `789.084 us`, maximum `3156.208 us` and one simulated deadline
  exceedance. At 48 kHz/128 (11,250 callbacks) it recorded zero covered
  `new` calls, p99.9 `1899.791 us`, maximum `4933.250 us` and two simulated
  exceedances. Both runs exited successfully with zero reported solver
  fallback/non-finite samples; over-deadline callback work remained near two
  Newton iterations per ladder sample. This probe does **not** intercept
  direct `malloc`, all platform allocators or work on other threads; snapshots
  outside the timed callback can affect subsequent scheduling. Zero covered
  `new` calls is not a complete allocation-safety pass, and both runs failed
  the approved zero-simulated-exceedance rule. Do not attribute outliers to
  scheduling, allocations or Newton work without further evidence.

  **Corrected probe window (27 September 2026):** After moving the probe's
  begin/end calls directly around `processBlock` (excluding the clock read
  afterward), an isolated 44.1 kHz/128-sample, eight-voice, 1x coupled run
  completed 10,336 callbacks with exit 0. Median/p99.9/max were
  `589.458/805.125/2933.875 us` versus a `2902.494 us` simulated deadline;
  one callback exceeded that deadline. The executable verified interception
  with a deliberate `new[]` before the run and recorded zero covered C++
  `new` calls during callbacks. The over-deadline callback used 4091 coupled
  iterations and line-search trials for 2048 ladder samples, with zero
  reported fallback/non-finite samples. The count covers calling-thread C++
  `new`, not direct `malloc`, other allocators or worker threads. Elapsed time
  includes the probe's begin/end calls, whereas the `new` count covers only
  `processBlock`. The one overrun fails the approved zero-exceedance rule;
  its cause is unproven.

  **Strengthened probe, 257-sample repetitions (27 September 2026):** The
  cost executable now verifies scalar `new`, `new[]` and aligned `new` with
  three deliberate allocations before the run. In sequential 30-second
  coupled-only runs with eight voices, 44.1 kHz/257 (5,148 callbacks) measured
  median/p99.9/max `1190.833/4221.084/7498.083 us` with **one** simulated
  deadline exceedance against `5827.664 us`; 48 kHz/257 (5,604 callbacks)
  measured `1176.625/1780.334/1972.125 us` with zero exceedances against
  `5354.167 us`. Both returned exit 0, reported zero covered callback `new`
  calls and zero solver fallback/non-finite samples. The slowest callback in
  each run used approximately two coupled iterations per ladder sample.
  The 44.1 kHz run fails the approved zero-exceedance timing rule despite its
  p99.9 being below the 75%-deadline threshold of `4370.748 us`. This does
  **not** erase earlier passing runs; it shows further tail variability.
  Probe scope and elapsed-time caveats above still apply.

  **Snapshot-mode comparison (27 September 2026):** The Release cost tool
  defaults to a final-only solver-work snapshot; an explicit trailing `work`
  argument for `candidate-coupled` enables the previous per-callback snapshots.
  Two sequential 30-second 44.1 kHz/128, eight-voice, 1x coupled runs each
  completed 10,336 callbacks (exit 0). Final-only had median/p99.9/max
  `591.166/3117.583/25880.083 us` and **11** simulated deadline exceedances;
  per-callback `work` had `594.209/957.458/3353.458 us` and **one** exceedance,
  against `2902.494 us`. Both reported identical output sum of squares
  (`231268.160`) and coupled work totals (21,241,856 samples; 42,467,693
  iterations and line-search trials), zero covered callback C++ `new` calls,
  and zero reported fallback/non-finite samples. The per-callback overrun used
  4085 iterations for 2048 ladder samples. Both fail the zero-exceedance rule.
  Sequential offline runs are not controlled scheduling experiments: neither
  these results nor the optional snapshot traversal identify a cause. Timer
  measurements still include the allocation probe's begin/end overhead.

  **Explicit measured timing verdict (27 September 2026):** The cost tool now
  prints `measured_timing_rules_met=1` only when unrounded p99.9 is strictly
  below 75% of the simulated deadline and there are zero simulated
  exceedances. The field is independent of tool exit status; exit 0 does not
  imply a timing pass. A strict-boundary unit test and coupled focused tests
  passed 123,380 assertions in five cases. Both candidate and ordinary
  Release cost tools build and emit the field. This is a per-run timing verdict,
  **not** the complete CPU/safety gate or proof of zero allocations across
  uninstrumented APIs and threads. Earlier benchmark logs predate this field;
  their recorded timings and failures remain unchanged. The full development
  suite after adding this rule passed 257 cases and 7,002,056 assertions
  (exit 0). This tests the development binary, not the full product gate.

  **Processor output comparison (27 September 2026):** A development-only
  nested-versus-coupled test now covers full stereo `processBlock` output at
  44.1 and 48 kHz with both 128- and 257-sample blocks. It uses multiple
  voices, mid-block note-on/off boundaries and changes to cutoff, resonance
  and drive. In all four deterministic configurations the maximum absolute
  sample difference stayed below `1e-4`, output remained finite and nonzero,
  reported coupled fallback/non-finite samples were zero, and latency agreed.
  The coupled-focused selection passed 197,520 assertions in five cases.
  This is focused numerical agreement, not a control-specific listening result, a full spectral
  comparison or permission to promote the solver. The full development suite
  with this comparison passed 258 cases and 7,076,204 assertions (exit 0).

  **Note-transition allocation probe (27 September 2026):** An explicitly
  selected `transitions` workload in the development Release cost tool
  alternates note-off and note-on at distinct note numbers across the selected
  voice lanes every eighth callback. It prepares the MIDI events before the
  timer and allocation-count window, then measures their handling inside
  `processBlock`; the existing sustained workload is unchanged. In sequential
  2-second 44.1 kHz/128-sample, **16-voice** 1x nested/coupled runs, each
  completed 690 callbacks including 87 transition callbacks (exit 0). Both
  self-tested the C++ allocation probe and reported zero covered callback
  `new` calls. Nested/coupled median was `2740.792/1271.625 us`, p99.9 was
  `3357.667/1550.000 us`, and simulated exceedances were `250/0`; coupled
  reported zero fallback/non-finite samples. These are distinct from the
  approved **eight-voice, 30-second sustained** CPU workload. Neither the
  short transition timing result nor zero intercepted `new` calls qualifies
  the CPU/safety gate: direct `malloc`, other allocators and other threads
  remain outside the probe.

  **Transition/tail split (27 September 2026):** The cost tool now labels
  simulated deadline exceedances and maximum callback times separately for
  callbacks with prepared MIDI transitions and those without. In a further
  sequential 2-second 44.1 kHz/128-sample, 16-voice nested/coupled pair
  (690 callbacks per engine; 87 with transitions), nested had 251 simulated
  exceedances: 35 transition callbacks and 216 of the 603 other callbacks.
  Nested maximums were `3353.667 us` on a transition and `4196.833 us`
  without one. Coupled had zero exceedances in either group; its maximums
  were `1421.208/1468.334 us` with/without transitions. Both recorded zero
  covered callback C++ `new` calls; coupled reported zero fallback/non-finite
  samples. The split totals and maximums were checked against the emitted
  rows, including a short sustained-workload regression. This is a diagnostic
  distribution in an unapproved 16-voice, 2-second workload, not evidence that
  transitions caused the tails or that the approved CPU/safety gate passes.

  **Nothrow allocation-probe coverage (27 September 2026):** The executable-local
  callback probe now also intercepts scalar, array and aligned nothrow C++
  `new`; its Release self-test checks seven ordinary/aligned/nothrow allocation
  forms before measuring callbacks. Candidate-enabled and ordinary Release cost
  tools built and passed that self-test. A short 44.1 kHz/128, eight-voice 1x
  coupled `transitions` run completed 104 callbacks, including 13 with MIDI
  transitions (exit 0), and recorded zero covered callback C++ `new` calls
  with zero reported solver fallback/non-finite samples. The ordinary build
  continues to reject development-only modes. A source scan found no explicit
  direct `malloc`/`calloc`/`realloc` calls in the inspected Mono processor,
  voice or ladder files. This is **not** proof about calls inside dependencies,
  other platform allocators, other threads or complete real-time safety;
  the short run does not qualify the approved CPU gate.

  **Four-configuration default-scheduler repetition (27 September 2026):**
  Sequential eight-voice 1x coupled Release runs used the approved 30-second
  sustained-note workload on the physical MacBookPro18,1, with `caffeinate -i`
  preventing idle sleep. The scheduler was otherwise unchanged; nearby `ps`
  snapshots showed substantial VS Code, WebKit and WindowServer CPU use.
  These are *not* controlled-scheduling or audio-device callback results.

  | Rate | Block | Callbacks | p99.9 / 75%-deadline (us) | Max (us) | Simulated exceedances | Measured timing rule |
  | --- | ---: | ---: | ---: | ---: | ---: | --- |
  | 44.1 kHz | 128 | 10,336 | 768.916 / 2176.871 | 966.792 | 0 | Meets in this run only |
  | 44.1 kHz | 257 | 5,148 | 2201.709 / 4370.748 | 9493.750 | 2 | Fail |
  | 48 kHz | 128 | 11,250 | 1178.833 / 2000.000 | 3722.334 | 2 | Fail |
  | 48 kHz | 257 | 5,604 | 6050.209 / 4015.625 | 16419.750 | 6 | Fail |

  All four tools exited 0, reported zero intercepted callback C++ `new`
  calls with the seven-form self-test, zero coupled unconverged/non-finite
  samples and zero reported latency samples. An exit of 0 is not a timing
  pass. The 44.1 kHz/128 pass does not override its earlier failures, and
  257-sample exceedances recur; the cause remains unknown. No configuration
  has passed the full CPU/safety gate, and control-specific listening remains pending.

  An earlier full development suite against the work-instrumented plugin binary
  passed 256 cases and 7,002,048 assertions (exit 0). The subsequent
  allocation-probe window change affects only the standalone cost executable;
  its candidate and ordinary Release variants built, its deliberate-allocation
  self-test passed, and the corrected 30-second run above completed with exit 0.
  This verifies neither untested allocation APIs nor product acceptance.
  Timing failures are not permission to weaken the approved target. The
  257-sample configurations met measured timing rules in their earlier runs,
  but the latest 44.1 kHz/257 repetition did not;
  **no configuration has passed the full CPU/safety gate**. Keep nested as the
  normal development selector and coupled opt-in until the required gates pass;
  coupled is the preferred replacement candidate, not an approved default.

  **Opt-in thread-CPU diagnostic (27 September 2026; not a gate):**
  `VektMonoProcessorCost ... candidate-coupled cpu` samples
  `CLOCK_THREAD_CPUTIME_ID` around the calling-thread callback in addition to
  existing wall time. A 30-second offline 44.1 kHz/128/eight-voice 1x run
  reported p99.9 `1175.458 us`, max `9154.625 us`, five simulated deadline
  exceedances against `2902.494 us`, and
  `wall_overruns_cpu_below_deadline=5`. The maximum recorded CPU time *among
  overruns* was `1916.750 us`; maximum estimated wall minus CPU was
  `8483.334 us`. Exit 0, zero intercepted C++ `new`, and zero reported
  unconverged/non-finite samples are not a timing or full safety pass. A
  separate 2-second 48 kHz/257 run likewise recorded three wall overruns,
  all with thread CPU below its `5354.167 us` simulated deadline. The 30-second
  run overlapped another short diagnostic and neither run controlled the
  scheduler or competing processes. These observations are compatible with
  descheduling but cannot establish its cause or rule out measurement overhead;
  no live device callbacks were measured. The CPU option changes measurement
  overhead, leaves the existing wall-time verdict unchanged and is not
  directly comparable to earlier unsampled runs. Evidence:
  `/tmp/vekt-coupled-thread-cpu-30s-20260927.log` (ephemeral).

  **Sequential thread-CPU repetition (27 September 2026; still uncontrolled):**
  On the same MacBookPro18,1, two Release cost-tool runs each processed 30
  seconds of *offline audio* sequentially with `caffeinate -i`, the default
  scheduler and CPU sampling enabled. `caffeinate` prevented idle sleep only;
  it did not isolate the processor or confer real-time scheduling. Process
  snapshots before the first run showed WindowServer and several VS Code
  helpers using CPU; their load varied before the second run.

  | Rate / block / eight voices / 1x coupled | Callbacks | p99.9 / 75%-deadline (us) | Max (us) | Wall exceedances | Exceedances with thread CPU below deadline | Measured timing rule |
  | --- | ---: | ---: | ---: | ---: | ---: | --- |
  | 44.1 kHz / 128 | 10,336 | 2649.959 / 2176.871 | 16797.625 | 8 | 8 | Fail |
  | 48 kHz / 257 | 5,604 | 3177.416 / 4015.625 | 4754.000 | 0 | 0 | Meets in this run only |

  For the eight 44.1 kHz overruns, maximum reported thread CPU was
  `1929.459 us` against a `2902.494 us` deadline; maximum wall minus thread
  CPU was `15261.041 us`. Both runs exited 0 with identical expected output
  energy and solver-work totals for their configurations, zero intercepted
  callback C++ `new` and zero coupled unconverged/non-finite samples. This
  strengthens the observation of off-CPU wall time in these instrumented
  callbacks, but neither identifies the reason for the gaps nor proves actual
  audio-device behavior or complete allocation safety. No controlled scheduler
  or low-contention environment was achieved; do not score either run as a
  full CPU/safety pass or soften the existing zero-exceedance target. Log:
  `/tmp/vekt-coupled-sequential-cpu-valid-20260927.log` (ephemeral).
- **Pending — coupled replacement and legacy retirement:** Preserve the legacy
  host plugin while auditioning and qualifying coupled. Keep nested for
  numerical comparison, not as an assumed release fallback. If the selected model
  needs changes, do them in the development instrument, then revisit the reference
  and literature alternatives. Resolve split playback/offline quality controls,
  every retained candidate path before cutover.
  After acceptance, switch the production path to coupled, validate all host
  formats, state/presets and audio regressions, then remove legacy implementation
  and development-only engine selectors in a separately verified cleanup.
  ADR 0005 remains Proposed until its sound, safety, CPU and product decision.

**Scope rule:** pre-alpha development integration is permitted before ADR
acceptance; do not equate it with acceptance or a released engine. Update this
track and the evidence ledger when builds, renders
or listening results are actually collected.

## Maintenance rule

This file is the live work plan, not a historical assertion that every task has
passed. **Update it in the same change as each new experiment, product decision,
changed assumption or gate transition.** Keep the current gate and next action at
the top accurate; set each step to `not started`, `in progress`, `blocked` or
`complete`. For every transition record the date, result and evidence location,
the decision or reason for blocking, and what remains. A running or interrupted
test is not a pass; a focused pass does not imply full-matrix completion. Reconcile
the status in `docs/MONO_VALIDATION.md` and ADR 0005 whenever it changes. Do not
rewrite earlier measurements as if they tested a newer model or binary.

## Product boundary and decision rules

- Goal: a convincing classic-style dry Mono bass/lead ladder with deliberate
  modern options, not an asserted match to a particular Moog or a Mother-32.
  Hardware measurements/SPICE are useful comparisons only unless the product
  reviewer explicitly revises this target.
- Retain raw fourth-stage output, actual nonlinear input drive, optional
  post-ladder compensation, nominal resonance-1 oscillation boundary, and
  cutoff/resonance/drive modulation unless an explicit ADR change approves
  a different mapping. The current candidate is the *specified* `tanh`-stage
  TPT/ZDF model, not a published circuit's exact equations.
- ADR 0001 plans separate candidate **Playback Quality** (1x/2x/4x/8x,
  default 1x) and **Offline Render Quality** (follows Playback Quality
  until explicitly overridden with 1x/2x/4x/8x). The legacy GUI has one
  four-choice setting; the separate controls are not implemented.
  A credible 1x default is **not** permission to ship unreviewed higher modes.
  Support conditions may be limited explicitly; a 1x-only or mixed-engine
  release requires an approved ADR 0001/0005 revision and quality-switch
  validation.
- Keep `production_integration_allowed=false`. Measurement-only integration
  can precede acceptance; shipped engine replacement cannot. If the approved
  contract proves infeasible, record rejection/supersession rather than
  changing thresholds after observing failures without review.
- Prefer a **single coupled production engine**, not a permanent legacy/coupled
  quality split. Treat nested and legacy as comparison baselines while coupled
  is qualified. If coupled cannot pass sound, stability, timing or host gates,
  keep legacy in production and revisit the model or explicitly revise the
  product contract; do not silently ship a mixed-engine fallback.

## Evidence ledger (not acceptance)

### Retained-quality audit (27 September 2026)

| Mode | Evidence obtained for coupled | Still needed before production approval |
| --- | --- | --- |
| 1x (default) | Development processor selection; focused numerical, finite-output, reset, latency and solver-work checks; Thomas's favorable unblinded audition; 30-second Release complete-processor *offline* runs with both passing and failing timing-rule outcomes. Covered C++ `new` probes reported zero callback allocations. | Broad reference-aligned spectral/IMD and control-modulation matrix; documented representative listening and gain/headroom; approved fallback and latency limits; full allocator coverage and long-run safety; repeatable controlled Release results meeting **all four** approved M1 Pro cases and live host/device evidence. No 1x operating claim is qualified. |
| 2x (IIR) | Explicit development selection, finite output, effective-engine and latency smoke checks; short 48 kHz/128/eight-voice Release probe had zero simulated exceedances in eight callbacks. | Approved rate/block/voice envelope and distinct timing rule; per-path sound, reference/spectral, modulation, stability, latency, long-run allocation/fallback and complete-processor Release/host evidence. Short probe is not a pass. |
| 4x (FIR) | Same focused development-path checks; short 48 kHz/128/eight-voice Release probe had zero simulated exceedances in four callbacks. | Same per-path gates as 2x, including FIR latency and host reporting/recall; four callbacks cannot qualify a tier. |
| 8x (FIR) | Same focused development-path checks; short 48 kHz/128/eight-voice Release probe exceeded its simulated deadline on all four callbacks. | Same per-path gates as 4x; investigate cost, define/test a support envelope on named hardware and obtain reviewer approval. Do not infer either a supported real-time tier or an irrevocable product failure from four callbacks. |

**Development recall check (27 September 2026):** A new processor test restores
each stored quality (indices 0–3) into a fresh coupled development processor,
checks effective engine and latency against a normal legacy instance, and
compares deterministic stereo output to the originating coupled processor.
It passed 12,404 assertions in one focused case; the development suite with
the new test registered passed 265/265 cases. This narrows the development
state/render gap; it does not validate a production coupled plugin, the
separate playback/offline controls or host-managed normalized snapshots.

**Development quality-transition check (27 September 2026):** A focused
processor test checks 1x→2x, 2x→4x, 4x→8x and 8x→1x as separate transitions at
48 kHz/128 samples. For each pair, the requested switch remains pending while
transport plays, while a note is sustained after stop, and through sustain
release; once idle it updates active quality and matches legacy's reported
latency. A new coupled note then produces finite output with zero reported
solver fallback/non-finite samples. The focused case passed 4,296 assertions.
The full development suite with this test registered passed 266/266 cases;
ordinary Release VST3 and Standalone targets built with development mode off.
This does not measure transition clicks, host latency renegotiation, real-time
allocation/CPU or playback/offline follow/override behavior.

**Preset-boundary diagnostic (27 September 2026; development-only):** A focused
coupled processor test loads factory program 24 (Classic Three Bass) after an
active note at each retained quality, checks program/parameter recall and
engine isolation, then renders empty callbacks and a fresh note. The initial
strict-silence assertion failed at 2x (first post-load block RMS approximately
`0.02033`); inspection shows preset loading resets voices, but does not reset
the oversampling bank's filter history. A revised diagnostic checks finite
post-load callbacks and silence after 32 empty blocks at all four qualities;
the final version passed 42,057 assertions in one focused run, including a
fresh-instance empty-block silence check. The final development suite passed
267/267 cases and ordinary Release VST3/Standalone targets still build with
development mode off. That historical test did not establish strict silence;
the subsequent product decision and implementation below supersede its open
tail-policy question. The existing legacy 1x preset-silence test did not cover
oversampled paths.

**Preset-load isolation decision and implementation (27 September 2026):**
Thomas chose strict isolation for *every* preset load (factory, user, host
program, next/previous): no old-preset output is permitted in the next audio
callback, even if an abrupt cutoff clicks. With no new MIDI note, every sample
of both channels must be exactly zero; a new note must sound immediately in
that same callback using the new preset. Project-state restore is a separate,
undecided policy. The shared successful-preset path now schedules a voice and
oversampling-filter reset at the start of the next callback, before MIDI.
A focused development regression exercises all five load routes at 1x–8x,
both with no note and with a note at sample 32; it passed 82,628 assertions.
The final development suite passed 267/267 cases; ordinary Release VST3 and
Standalone targets built with development mode off. The normal plugin still
selects legacy. Host timing/click behavior, concurrent preset-load safety and
production acceptance remain unqualified.

For **all** rows, playback/offline split and follow/override recall are planned,
not implemented. The normal host plugin still selects legacy at every quality;
focused development tests do not establish VST3/AUv3/Standalone lifecycle or
actual device deadlines. Preserve the dated raw results in
`docs/MONO_VALIDATION.md`; this audit does not rescore earlier runs.

| Evidence | Verified scope | Unresolved implication |
| --- | --- | --- |
| Cutover-planning baseline verification (27 September 2026) | At source revision `d68aae9` plus documentation-only working-tree edits, the `dev` Debug arm64 configuration (`VEKT_MONO_LADDER_DEVELOPMENT=ON`) built its existing test executable with no work and `ctest --preset dev --output-on-failure` passed 264/264 tests in 232.25 seconds. The ordinary arm64 `release` configuration (`VEKT_MONO_LADDER_DEVELOPMENT=OFF`) built VST3 and Standalone targets. See `docs/MONO_VALIDATION.md` for ephemeral logs. | These are current baseline build/tests, not a coupled production binary, host/device test, accepted sound or CPU/safety result; no acceptance gate changed. |
| Development suite before the latest 1x comparison | 246 cases, 6,645,095 assertions, exit 0; see `docs/MONO_VALIDATION.md` | Predates the 1x diagnostic; not a product gate. |
| Focused 1x diagnostic | 120 assertions; coherent 48 kHz tone, 1/2/4-step outputs and 16/32-step reference returns; see `tests/audio_lab/LadderPrototypeTests.cpp` | Reference returns have no reconstruction low-pass; neither alias attribution nor audibility is settled. |
| Filtered 1x reference probe (26 September 2026) | 38,578 assertions in one focused case. Offline 16x/32x internal samples pass through a symmetric Blackman-windowed sinc low-pass before host decimation; sampled 1–15 kHz passband and 25–47 kHz stopband, injected 47 kHz fold and exact last-substep/host-return equivalence are checked. At 48 kHz, resonance 0.98, 7 kHz input and +12/+24 dB drive, the 1 and 5 kHz reference bins change by less than 0.5 dB from 16x to 32x; see `tests/audio_lab/LadderPrototypeTests.cpp` and `docs/MONO_VALIDATION.md`. | One tone, two inspected output bins, 1 kHz-spaced filter-response points and no 32x-to-higher-factor full-render convergence or full-band error budget. Not a listening or product pass. |
| Release cost probes | Half-second, filter-only M1 Pro diagnostics in `docs/MONO_VALIDATION.md` | Not a complete candidate processor, 30-second tail qualification or actual audio callbacks. |
| Coupled 1x development audition (27 September 2026) | Thomas reports that coupled 1x sounds much better than legacy and favors removing legacy; see `docs/MONO_VALIDATION.md`. No further conditions or control-by-control observations were supplied. | This establishes a positive 1x sound preference, not control-specific listening, higher-mode acceptance, production cutover or legacy deletion. |
| Reviewer and approval | Thomas identified himself as product owner and single developer on 26 September 2026; approved the sound target and 1x CPU target, preferred coupled on 27 September and reported a favorable unblinded 1x audition. | Numerical sound limits, representative listening, higher-mode operating envelopes and final sign-off remain pending. |

## Ordered steps and checkpoints

### 1. Freeze the comparison specification and acceptance contract — blocked

**Owner:** Thomas, product owner and single developer (identified in conversation
on 26 September 2026), supported by DSP engineering. The revised unblinded
listening method is approved; its representative checks remain pending.
Record which rate/block/voice combinations per quality must be real-time on the
physical M1 Pro and which require separately measured faster hardware,
acceptable audible artifacts, provisional alias/IMD/modulation
limits (including 1x and overload), self-oscillation/startup, stopband,
latency, fallback and allocation/CPU policies. Agree representative cases and
boundary coverage rather than a blind Cartesian product. The proposed
`-60/-80 dBc` limits are unapproved; Thomas approved the 75%-of-deadline
target for the four specified 1x combinations only.
**Done when:** reviewer, quantitative rules, exceptions and decision authority
are approved and recorded in ADR 0005. **Remaining:** approval of the
contract, numerical review policy and operating envelope; complete the
representative listening checks separately.

#### Step 1 decision sheet — prepared for review, not approved

**Frozen context:** ADR 0005 fixes the raw fourth-stage ladder output, actual
input drive, optional post-ladder compensation, nominal resonance-1 onset and
required cutoff/resonance/drive modulation. ADR 0001 plans separate candidate
Playback Quality (1x/2x/4x/8x, default 1x) and Offline Render Quality
(follows playback by default; explicit override supports 1x/2x/4x/8x).
Interim legacy has one four-choice quality control. No mode promises
glitch-free operation in every combination. The physical Apple M1 Pro
(MacBookPro18,1, 32 GB) is the stated
measurement target. Neither existing filter-only timings nor the complete
*legacy* processor benchmark measure the complete candidate processor.

| Decision for the product reviewer | Existing proposal or constraint | Required signed response |
| --- | --- | --- |
| Decision authority and sound target | **Approved by Thomas, product owner and single developer, 26 September 2026:** convincing classic-style dry bass/lead ladder with controlled overload and smooth resonance; no claimed match to a particular Moog or Mother-32. Hardware/SPICE comparisons remain diagnostic. This does not approve any candidate. | No decision pending on the target itself; candidate sound decision remains pending. |
| Replacement direction | **Approved by Thomas, 27 September 2026:** coupled is the preferred solver to qualify for a single-engine replacement; retire legacy promptly after accepted coupled integration and full regression checks. Nested remains a diagnostic comparator. | Approve candidate sound and every retained path, CPU/safety and host behavior before switching production; verify cutover and legacy-code deletion separately. No cutover is approved yet. |
| 1x default alias/artifact policy | **Review method approved by Thomas, 26 September 2026; listening method revised 27 September; no numerical ceiling approved.** Characterize Off separately rather than applying the proposed 2x/4x ceiling. A single 48 kHz driven case has excess 1/5 kHz bins relative to a narrow filtered reference. | Complete the broad reference-aligned comparison and documented unblinded listening checks; then separately decide a numerical ceiling or documented listening-led exception. Do not infer a pass from the probe. |
| Higher qualities | **Revised 27 September 2026:** Playback Quality (1x/2x IIR, 4x/8x FIR; default 1x); planned Offline Render Quality follows playback unless overridden at up to 8x. The legacy plugin has one four-choice control; separate controls are not implemented. The former offline-16x host/export policy is superseded. Stored 16x project state is rejected in full, not silently recalled at 8x. | Approve latency and operating envelopes for each retained quality; test recall, effective-quality reporting and host behavior. |
| Host rates and real-time envelope | **Approved target by Thomas, 26 September 2026:** the future 1x candidate must meet the agreed real-time CPU rule on the physical M1 Pro at **44.1/48 kHz × eight active voices × 128/257-sample blocks** (all four combinations). The 48 kHz/257/8 pilot remains the first measurement. This is a target, not evidence of a pass. | Measure and explicitly constrain every other exposed rate/block/voice combination before release; determine the retained 2x/4x/8x operating envelopes separately. No candidate processor timing qualifies any combination yet. |
| Hardware tiers and maximum settings | **Policy approved by Thomas, 27 September 2026:** keep the above M1 Pro 1x baseline target; higher polyphony and 2x/4x/8x may have narrower M1 Pro envelopes or require faster hardware. A selectable setting is not a real-time claim at every rate, block size and voice count. | Define tested rate/block/voice/quality combinations and actual machine identifiers per tier; approve higher-mode timing rules and user-facing limits only after repeatable complete-candidate measurements and host tests. Do not infer faster-machine support by scaling M1 Pro results. |
| Cost, safety and latency | **1x target approved by Thomas, 26 September 2026, for all four approved M1 Pro combinations:** 30-second complete-candidate Release runs, zero processing-thread allocations and solver fallbacks, p99.9 below 75% of each block deadline, zero simulated deadline exceedances; separately report maximum and latency. Offline timings are not device callback measurements. | Approve latency bound, allocation instrument and measurement protocol; approve separate 2x/4x/8x rules. This is not a candidate pass. |
| Spectral and nonlinear limits | Proposed `-60 dBc` 2x and `-80 dBc` 4x for a 0.5-peak, +12 dB tone, not overload; IMD `0.03 dB` fundamental/`1e-3` absolute product; modulation unexpected spur `-60 dBc` relative to the largest expected component. No approved 8x or overload limits. | Approve/revise separate limits for 1x/2x/4x/8x, high drive and modulated cases after reference and listening review. |
| Stopband, self-oscillation and startup | Proposed deep-stopband absolute error `1e-7` when analytical peak is below `1e-6`; otherwise `0.01 dB`/`0.001 rad`. The proposed 1 kHz onset/tail rule does not apply at 10 Hz or the cutoff ceiling. | Approve error policy and cutoff-specific tail, startup, frequency and purity limits (including noise seeding). |
| Fallback and matrix sampling | ADR requires zero incidence in the supported matrix or an accepted continuous policy. The listed levels, controls and stimuli are a coverage plan, not an unreviewed Cartesian product. | Approve fallback behavior and named representative/boundary combinations per supported quality; specify any exclusions. |
| Listening and decision record | **Revised by Thomas, 27 September 2026:** the 26 September blinded protocol is superseded. Thomas is the named reviewer. His unblinded coupled 1x audition establishes a positive sound preference, not control-by-control verification. Document unblinded checks on representative patches and settings at 1x/2x/4x/8x. Blinding, randomization, level matching and three repeats are optional diagnostic methods, not release gates. | Record patch, quality, rate, controls, monitoring conditions, audible artifacts, gain/headroom observations and preference; sign off the scoped results in ADR 0005. Use a blind comparison only if a sound decision is uncertain. |

**Why the approved measurement pilot is not a release result:** At 48 kHz,
257 samples and eight active voices, the complete **legacy** processor's
30-second offline run reports a `1044.50 µs` median, `2681.04 µs`
99.9th percentile and `4746.67 µs` maximum against a simulated
`5354.17 µs` deadline, with no simulated exceedances. This is the only
30-second eight-voice, complete-path baseline recorded at that exact
combination; it is **not** a candidate result and does not prove spare
capacity for the nonlinear filter or actual callback safety. Benchmark
the same configuration with the complete candidate after sound feasibility,
then cover all four approved 1x target combinations against the approved
CPU *target*. Other host rates, block sizes (including 1/16/31/32/127
samples), and 12/16 active voices have **no approved real-time claim**;
they are neither removed nor silently approved. Measure them and record
explicit operating limitations before candidate release. The planned five
host rates and voice-count choices are not reduced by this CPU target.

**Hardware-tier policy (approved 27 September 2026; envelopes not yet
qualified):**

| Tier | Intended operating claim | Qualification still required |
| --- | --- | --- |
| M1 Pro baseline | Future candidate 1x, eight active voices, 44.1/48 kHz and 128/257-sample blocks: retain the approved 30-second timing and safety target for **each** combination. | Controlled, repeatable complete-candidate Release runs and live-host/device checks. Recent default-scheduler offline passes and failures do not qualify this tier. |
| Extended playback | 12/16 voices, other rates or blocks, and 2x/4x/8x playback: M1 Pro coverage may be narrower; demanding combinations may be supported only on a specified faster **measured** machine. | Freeze per-quality rate/block/voice cases, tested hardware and timing policy with the reviewer. Measure the full candidate on every claimed boundary; disclose exclusions. No faster-machine tier is qualified today. |
| Offline render | Planned override follows playback by default or selects up to 8x. | Validate follow/override recall, bounded render time/work, finite output, allocation/safety, sound, latency and deterministic export; do not imply a playback deadline from offline timings. |

CPU capability is hardware- and workload-dependent; finite output, bounded
work, processing-thread allocation safety, approved fallback policy, spectral
and listening requirements are not waived by a narrower envelope or faster
hardware. Profile complete-processor and callback-tail causes, including
competing processes and scheduling, before considering optimization or a
change to the M1 Pro 1x baseline. Any baseline threshold or envelope revision
requires a separate, attributable product decision in ADR 0005; a failing run
is not permission to revise it. Publish only tested machine/configuration
claims and clearly describe unsupported or unqualified combinations.

**Response process:** Record the decision-maker's name and role and the date of
each actual decision in ADR 0005 with an attributable review record (this
conversation records the scope, pilot, CPU-target and sound-target decisions).
Revised targets must be written *before* scoring new runs or explicitly marked
as post-measurement changes. Record unresolved rows as
pending; do not mark step 1 complete until all rows are settled and this file
and `docs/MONO_VALIDATION.md` reflect the approved contract. Approval of a
review method is not acceptance of the candidate or of remaining listening checks.

**1x artifact decision method (approved by Thomas, 26 September 2026;
not a numerical limit or sound approval):** Compare
same-input, raw-tap candidate and progressively converged, band-limited
higher-rate reference renders with matched level and latency. Report the
largest *reference-attributed excess* host-band component, its frequency,
absolute amplitude and dBc relative to the fundamental, plus the reference
error floor. Separate direct harmonics and expected modulation sidebands
from unexpected components. Cover independent absolute input frequencies,
all planned host rates, low/near-oscillating resonance, 0/+12/+24 dB drive,
and static and audio-rate-modulated controls; include the known 48 kHz
7 kHz input / 1 and 5 kHz discrepancy and overload. If the reference cannot
resolve a component above its uncertainty, mark it inconclusive. Document
unblinded listening checks on representative patches and settings with the
named reviewer; log conditions, artifact audibility and musical preference,
including cases where measurements and listening disagree. Only after that
evidence should Thomas freeze a separate 1x numerical ceiling or explicitly
approve a listening-led exception. The existing two-bin filtered probe does
not pass this method; approval of the method alone does not approve a sound.
Thomas is the appointed reviewer and also the developer. His 1x preference
is recorded, but the control-specific and higher-quality checks remain open.
Keep raw renders for numerical gain and headroom review. Optional level/latency
matching or blind comparisons can resolve uncertain sound decisions; neither
is a prerequisite for sign-off.

**Historical host-export feasibility checkpoint (superseded by the 27 September
2026 removal of offline 16x; not a current acceptance gate):** JUCE's normal plugin
`processBlock` returns audio rather than a host export-cancellation status.
Thomas selected **Ableton Live 12 Suite + VST3** as the first investigation
target on 26 September 2026. Locally installed Live reports version 12.4.6;
an arm64 Vekt Mono VST3 artifact exists, but it has only the interim single
quality control and no offline guard or export-cancellation test harness.
Neither the existing binary nor Live's export documentation proves the
approved contract. Ableton documents that export can be offline *or* real-time
when routed through external hardware; this case must be covered.
VST3 specifies `kRealtime`, `kPrefetch`, and `kOffline` process modes;
realtime-to-offline changes require `IAudioProcessor::setupProcessing`, while
realtime-to-prefetch changes may occur without it. Observe what the JUCE
wrapper actually conveys to `prepareToPlay`/`isNonRealtime` in Live. In the
vendored JUCE VST3 wrapper, `setupProcessing` calls `setNonRealtime` and
`preparePlugin(..., CallPrepareToPlay::no)`; `setActive(true)` calls
`preparePlugin(..., CallPrepareToPlay::yes)`. Thus even a conforming offline
`setupProcessing` does **not** satisfy Mono's chosen reinitialization-only
quality-switch boundary without a subsequent activation/preparation.
Neither the standard nor Ableton documentation proves export cancellation
from a plugin.

For this exact host/version/format, capture an attributable run log and the
presence/absence of exported files for each case:

1. Playback at 1x/8x and a saved unavailable 16x offline preference: ensure
   playback does not activate 16x; record requested and effective quality.
2. Offline export without an override and with 16x override: check VST3
   process mode at setup and per callback, the JUCE mode at preparation and
   during processing, and the exact ordering of `setupProcessing`,
   `setActive(false/true)` and `prepareToPlay`. Do not qualify 16x if Live
   only changes VST3 mode without reactivation; record output, latency and
   deterministic rerenders. Do not infer offline status from transport
   being stopped or from export alone.
3. Export routed through external hardware (Live's real-time-render case):
   ensure 16x is not activated; verify visible status, saved preference and
   handling of the export file.
4. In an unverified host, recall a stored 16x override and attempt export.
   Require an enforceable preflight or cancellation with **no invalid output
   file**, a visible explanation and unchanged saved preference. A silent or
   8x file does not pass. Simulate offline-status loss while 16x is active;
   verify the exceptional no-16x guard and reinitialization requirement
   without changing latency or DSP state in a callback.

Before listing any host as eligible for offline 16x, demonstrate its actual
offline-status lifecycle, reinitialization, latency negotiation and an
attributable preflight/cancellation path that prevents creation of an invalid
export when an unavailable saved 16x preference is recalled. Verify the
host-visible explanation and that the stored preference survives. If no
supported host supplies such a mechanism, revisit the product requirement
with Thomas; never label a silent file or warning as a blocked export.
**Status:** not run. The installed Live version and existing VST3 binary were
inspected, but the binary is legacy and cannot exercise the future separate
quality controls or export-block rule. There is no qualified-host list,
instrumented mode probe or plugin export-block mechanism; this checklist is
not evidence of passing host behavior. If no mechanism exists, escalate a
product-scope revision rather than create a silent or mislabeled file.

### 2. Build a trustworthy 1x comparison reference — in progress

**Owner:** DSP engineering. Define a same-input, raw-tap, progressively converged
higher-rate render with explicit input interpolation, reconstruction/decimation
filter response, latency and fundamental alignment. Check filter stopband
and passband errors, substep *render* convergence (not only solver residuals),
and the host-band error floor. Compare intended harmonics/sidebands separately
from unexpected components. Reproduce the observed 48 kHz 1 kHz/5 kHz bins;
include independent absolute frequencies and 44.1/48/96 kHz, resonance,
overload and modulated cases. Record raw renders and measurement configuration.
**Stop:** if reference error cannot be bounded at a claimed bin, label that
bin inconclusive rather than changing the candidate to chase it.
**Progress (26 September 2026):** The offline reference now exposes its raw
internal substep samples without changing the default host-rate API. A
non-causal offline low-pass/decimation probe checks 1 kHz-spaced
1–15 kHz passband and 25–47 kHz stopband points, rejects an injected 47 kHz fold, and
compares 16x/32x results at 1 and 5 kHz for one settled 48 kHz tone.
The focused test passes 38,578 assertions; a combined filtered-reference
and one-step reference-compatibility selection passes 50,878 assertions
in two cases. Reference host returns remain
bit-exact when internal samples are captured. These checks do not establish
transition-band performance, inter-bin extrema, full-render or full-band
reference convergence.
See the evidence ledger and `docs/MONO_VALIDATION.md` for values.
**Done when:** reference convergence and filtering error are documented for
the chosen 1x feasibility cases. **Remaining:** filter transition-band
and between-grid response, higher-factor full-render convergence, independent
rates/frequencies, overload and modulation coverage, then reviewer criteria.

### 3. Specify at most two literature-informed alternatives — not started

**Owner:** DSP engineering. Evaluate *on paper* before implementation:

1. **Antialiasing route:** use Paschou et al. (2017) as a guide to an
   antialiased *stateful filter formulation*, not a `tanh` swap. Write the
   continuous equations, antiderivative/difference quotient, near-equal-input
   limit, state and solver updates, latency, control interpolation and bounded
   work. State explicitly where its circuit-derived model differs from ADR 0005.
2. **Model/loop route:** use D'Angelo and Välimäki (2014, Part II, with errata)
   to specify an alternative circuit-derived generalized ladder and
   non-iterative loop approximation. Map input voltage/gain, cutoff,
   resonance, feedback, four-stage output and modulation to Mono. Its
   operating-point linearization is not an exact solve of ADR 0005's
   equations. Do not import its published CPU or fidelity claim as ours.

The 2013 D'Angelo/Välimäki paper is historical context, not a third candidate
by default. The 2026 Oyama comparison is a caution that SPICE/linear/harmonic
rankings need not predict aliasing, equal-CPU behavior or listening preference.
**Done when:** each route has equations, mapping, numerical/alias risks,
estimated bounded work and a small implement-or-drop test; choose at most
two for coding. **Remaining:** both specifications and the selection.

### 4. Sound feasibility and documented listening — in progress

**Owners:** DSP engineering (renders), named product reviewer (listening).
Compare the unchanged 1x candidate and at most two alternatives at the same
input, controls, raw output and validated reference tap. Evaluate linear
gain/phase, overload harmonics/IMD, self-oscillation, modulation, worst
unexpected host-band components and fallback. Document unblinded listening
on representative patches and settings drawn from
`docs/MONO_VALIDATION.md`, preserving unmatched originals; record reviewer,
conditions, differences, preference and confidence. Do not claim a
hardware-accuracy winner from these tests.
**Stop/go:** if no 1x sound merits complete-path measurement, revisit model
or product target; do not default to another 2x CPU optimization.
**Done when:** documented 1x sound go/no-go against signed criteria.
**Remaining:** reference, alternatives, control-specific listening and decision;
Thomas's positive coupled 1x preference is already recorded.

### 5. Complete candidate-processor 1x cost and safety — in progress

**Owner:** DSP engineering; reviewer approves remaining operating limits.
An explicit, development-only 1x selection now exercises the Mono voice and
processor without enabling the candidate in the host entry point. Smoke tests
and a 10-callback complete-processor Release diagnostic pass their limited
checks (see the provisional integration track). This precedes sound approval
and does **not** qualify a release engine. Verify gain/control mapping,
processing-thread allocation, bounded work and longer note/control cases.
Use coupled as the primary qualification subject and nested as an equation-
agreement/cost comparator; do not substitute nested's results for coupled's.
On the M1 Pro compare candidate and legacy Release processor runs first at
the 48 kHz/257-sample/eight-voice pilot, then at all four approved 1x
combinations (44.1/48 kHz × 128/257 samples × eight voices).
Investigate the inconsistent callback tails using a documented controlled
scheduling protocol, repeated runs and live-host/device measurements; identify
the complete-processor bottleneck before changing solver or sound decisions.
Probe other exposed rates, blocks and voice counts to support explicit
limitations; include modulation/overload and long runs. Record build,
timings, simulated deadlines and any device tests separately. The default
`VektMonoProcessorCost` measures legacy; its explicit development-only
`candidate` mode measures the complete 1x processor in a separate build.
`VektLadderCost` is filter-only. Short diagnostics cannot establish feasibility.
**Done when:** reviewer-approved 1x complete-path cost/safety go/no-go is
recorded. **Remaining:** sound review, allocation checks, long-duration
candidate results and all four approved configurations.

### 6. Qualify retained qualities and close model gates — blocked

**Owners:** DSP engineering and product reviewer. For each retained
2x/4x/8x mode establish approved operating conditions, reference,
spectral, stability, latency and complete-processor Release evidence.
Development-only coupled selection now reaches 2x/4x/8x in the preview and
cost harness, with an explicit effective-engine label and focused finite-output,
latency and solver-work smoke coverage. Nested preview above 1x and the normal
plugin remain legacy. These smoke checks do not satisfy the
per-path matrix, Release CPU/safety budget or listening requirements.
Short 48 kHz/128-sample/eight-voice Release harness diagnostics (0.01–0.02 s)
reported zero covered callback `new` calls and finite energy: 2x had zero
simulated deadline exceedances in eight callbacks, 4x had zero in four, and
8x exceeded the simulated deadline in all four. The harness's generic
`measured_timing_rules_met` flag is not an approved higher-mode timing rule;
none of these short probes qualify a quality tier or an actual device callback.
Measure any claimed higher-polyphony or higher-oversampling real-time envelope
on the actual named hardware, with its rate, block and voice boundaries;
do not extrapolate from filter-only probes or presumed faster-CPU scaling.
Preserve zero processing-thread allocation, finite-output, bounded-work and
approved fallback safety gates for every retained setting, including settings
without an M1 Pro real-time claim. The reviewer must approve distinct
higher-mode timing rules rather than silently applying or relaxing the 1x rule.
Validate planned independent Playback and Offline Render Quality parameters
(each at most 8x), follow/override recall, effective-quality reporting and
host behavior for every retained mode. Earlier offline-16x host/export gates
above are superseded, not open product requirements.
Complete high-cutoff reference convergence, stopband tolerance,
self-oscillation/startup, IMD/modulation, zero fallback or an approved
continuous policy, long-run and allocation checks. Do not treat a
filter-only or single-bin win as a path pass.
**Done when:** signed matrix and every retained path meet their frozen gates,
or the product explicitly revises ADR 0001/0005. **Remaining:** full gate.

### 7. ADR decision and production handoff — blocked

**Owner:** named product reviewer. Record numerical/CPU approval, documented
unblinded listening checks, exceptions, scope and build identifiers in ADR 0005.
Accept only after all retained paths and model gates pass; otherwise reject,
supersede or explicitly revise the product scope. Exercise coupled across the
retained playback and offline quality paths in development before acceptance;
only after acceptance replace the host-created legacy engine when the approved
envelopes, zero-allocation/finite-output safety and host lifecycle gates hold.
Validate plugin formats, effective quality/latency and export restrictions,
project/preset recall, gain/headroom, render comparisons, and production builds
without development flags. Keep legacy available as a test comparator until
the new production path passes full regression checks; then remove legacy DSP,
obsolete selectors and legacy-only fixtures in a separately validated cleanup
rather than leaving a permanent mixed-engine release. Record build IDs and the cutover and
deletion decisions. If any gate fails, retain legacy production and report
the blocker; do not equate a development preview with replacement.
**Remaining:** coupled sign-off, integration, host/regression validation and
legacy removal.

#### Staged coupled cutover and rollback contract (objective, not approval)

1. **Prepare without switching production — in progress.** Keep the normal
   entry point legacy. Exercise coupled in the development processor at 1x
   first, then each retained 2x/4x/8x mode; run full Debug tests and build the
   ordinary Release plugin. Add production-equivalent state/preset, quality
   switching, effective latency, silence/reset and render comparisons without
   mistaking a preview binary for a production host test. Record exact source
   revision, flags, hardware and logs for each run. A focused development
   per-quality recall/render and sustained-note quality-transition regressions
   now pass; an oversampled preset-tail issue is under investigation. Preset
   policy, broader transition and production-host validation remain open.
2. **Freeze and measure — blocked.** Thomas approves numerical/listening,
   fallback, latency and per-quality operating rules *before scoring* results.
   Require reference convergence, spectral/IMD/modulation, resonance/startup,
   finite/bounded/zero-allocation long runs, and documented listening per mode.
   For 1x retain the four M1 Pro combinations and 30-second p99.9 <75% of
   deadline, zero simulated exceedances, allocations and fallbacks; repeat
   under a documented controlled scheduling protocol and test a live device.
   For 2x/4x/8x record separately approved timing rules, tested machine and
   rate/block/voice boundaries; include actual host callbacks for claimed
   playback support. Offline-only claims require bounded deterministic export
   and correct effective quality/latency, not a playback deadline.
3. **Decide — blocked.** Thomas records an attributable go/no-go with build ID,
   per-mode evidence, supported/excluded combinations, remaining risks and
   exceptions in ADR 0005. Keep `production_integration_allowed=false` until
   every retained path and host/state contract is accepted. If 8x or another
   mode cannot qualify, retain legacy production and explicitly revise ADR
   0001/0005 and the quality contract before any narrower cutover; do not
   silently route that mode to legacy.
4. **Cut over and verify — blocked.** In a reviewable change, enable coupled
   for the ordinary host-created plugin *without development flags*. Validate
   Release Standalone/VST3 and AUv3 where built, host lifecycle and live
   audio, playback/offline quality (including follow/override once implemented),
   reported latency, project and preset recall, gain/headroom, all retained
   modes, factory content and full regression tests. Explicitly test older
   host-managed normalized quality snapshots: rejecting stored index 4 project
   state cannot intercept those snapshots. Record the resulting build ID.
5. **Rollback/cleanup — blocked.** Until production-format and state regressions
   pass, keep a known-good legacy production build/source revision available;
   on a safety failure (non-finite output, unapproved fallback or allocation),
   missed approved timing/host boundary, incorrect latency/recall, or rejected
   sound, stop promotion and restore that legacy path in a new verified build.
   Do not switch engines mid-session or silently recall a different quality.
   Record the failure, rollback revision and regression result. Only after the
   coupled production build passes and Thomas records a separate deletion
   decision, remove legacy DSP and development selectors and rerun the full
   production regressions. Rollback after deletion requires restoring the
   preserved legacy revision and revalidation, not an undocumented runtime
   fallback.

## Research to inspect before implementing alternatives

- S. D'Angelo and V. Välimäki, *An Improved Virtual Analog Model of the Moog Ladder Filter*,
  ICASSP 2013, DOI 10.1109/ICASSP.2013.6637744 (historical comparator).
- S. D'Angelo and V. Välimäki, *Generalized Moog Ladder Filter: Part II—Explicit
  Nonlinear Model through a Novel Delay-Free Loop Implementation Method*,
  IEEE/ACM TASLP 22(12), 2014, DOI 10.1109/TASLP.2014.2352556 (check errata).
- E. Paschou, F. Esqueda, V. Välimäki and J. Mourjopoulos, *Modeling and
  Measuring a Moog Voltage-Controlled Filter*, APSIPA ASC 2017,
  DOI 10.1109/APSIPA.2017.8282295.
- H. Oyama, *Quantifying Nonlinear Behavior in Digital Moog Ladder Filters:
  Cross-Implementation Comparison and Common-Core Ablation*, DAFx 2026.

These works motivate controlled comparisons, not a claim that the current
candidate implements their methods or that any newer model is universally
more accurate, cheaper or more musical. `docs/MONO_VALIDATION.md` remains
the detailed evidence log; ADR 0005 remains the authoritative decision.
