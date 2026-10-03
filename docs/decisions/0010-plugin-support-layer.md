# ADR 0010: Plugin Support Layer and Architecture Programme

> Kobber was called **Mono** until 3 October 2026. Dated entries and records below keep the names of their time:
> Mono, `vekt::mono`, `plugins/vekt_mono`, `Mono*` types, `[mono]` and `[mono-*]` tags, test names "Mono ...", tools
> `VektMono*` (now `VektKobber*`), `VEKT_MONO_*` variables (now `VEKT_KOBBER_*`), and the Audio Lab tools
> `VektRavAudioLab` and `VektRavRender` (now `VektAudioLab` and `VektRender`).

## Status

Accepted (2 October 2026) by Thomas after an architecture review. In progress; the programme status below is the
persisted plan for the work and is kept current until every step is done.

## Context

The architecture names a `plugin_support` layer between the products and the framework, but none existed. Each
product processor carried its own host integration: preset catalog, user repository and session wiring, the host
program API, project state save/restore, previous/next preset navigation and, in Rav and Glimmer, the
tracking/offline oversampling request. The copies had drifted: Rav kept its own preset index, snapshot and program
number beside `PresetSession`, Glimmer read the host program from a `currentFactoryPreset` metadata key, and Mono
derived it from the session. Rav and Glimmer also deferred quality changes until the transport stopped, while Mono
applied them at once and the architecture said activation was not an audio-thread operation.

With three products, the shared host integration meets the two-consumer rule.

## Decision

1. **Quality changes are instant.** A requested oversampling quality applies at the start of the next audio block,
   during playback too. Audio may drop or click at the switch; no smooth transition is required. The new latency is
   reported through `setLatencySamples`, which the VST3 and AUv2 wrappers forward to the host on the message thread.
2. **One source of preset selection.** Every product derives the host program and the selected preset from
   `PresetSession`, saved as `vektPresetSelection`. The `currentFactoryPreset` metadata key is no longer written and is
   removed when an older project is restored.
3. **`framework/plugin_support` is an owned component, not a base class.** Each processor owns its host-integration
   component and forwards the host's program and state calls to it, keeping processors as composition roots.
   Products supply their preset descriptor, sound adapter and any extra project metadata (Rav's stage order).
   Only parts with at least two consumers move: the preset host (catalog, user repository, session, `StateManager`,
   program API, adjacent navigation, state save/restore), the tracking/offline quality request (Rav, Glimmer) and
   `requireParameter`. Since 3 October 2026 Mono uses the shared quality selection too (ADR 0001, uniform quality
   choices).

## Programme

Steps run in this order; changes stay unstaged for Thomas to commit.

| Step | Scope | Status |
| --- | --- | --- |
| 0 | This record | Done |
| 1 (A) | Instant quality switching in Rav and Glimmer; contracts in `ARCHITECTURE.md`, `UI_UX.md`, ADR 0001 | Done 2 Oct 2026; host gate open |
| 2 (D) | Mono reads parameters through cached pointers; reference renders bit-identical | Done 2 Oct 2026 |
| 3 (C) | Rav and Glimmer program state from `PresetSession` only; named sound-schema constants; new state fixtures | Done 2 Oct 2026; host gate open |
| 4 (E) | Product-qualified includes, no relative `../../` includes, white-box access declared in CMake | Done 2 Oct 2026 |
| 5 (F) | Measure incremental rebuild/relink, then (after Thomas confirms) split the tests into framework, rav, glimmer, mono, compat and audio_lab executables plus a `vekt_tests` aggregate | Not done: Thomas chose to skip it on the measurement below (3 Oct 2026) |
| 6 (B) | `framework/plugin_support`; migrate Glimmer, Mono, Rav | Done 3 Oct 2026; host gate open |
| 7 (G) | `ARCHITECTURE.md` dependency graph matches the build; product detail moves to product documents | Done 3 Oct 2026 |
| 8 | Uniform quality choices (ADR 0001, 3 Oct 2026): shared `OversamplingChoices`; Mono uses `QualitySelection` with Tracking/Offline including 16x; Mono baselines replaced | Done 3 Oct 2026 |
| 9 | Per-sample state correct at the highest internal rate (rule in ARCHITECTURE.md, `dsp::maximumInternalSampleRate`); Mono state to double; Rav low-cutoff one-poles to double; Mono references recaptured plus an offline case; shared `ui::QualitySettings` pop-over in Rav and Mono | Done 3 Oct 2026; visual and listening gates open |
| 10 | Mono noise held at the host rate with a host-rate pink corner; Rav coefficients cached on their controls | Done 3 Oct 2026; listening gate open |

**Next action:** the open release gates below. Step 6's `QualitySelection` publishes the active quality through an
atomic, so the editors no longer read `OversamplingBank`'s plain fields while the audio thread may switch them.

**Step 5 measurement (2 October 2026, Debug `dev`, 10-core Apple Silicon, one 57 MB `vekt_dsp_tests`):** an
incremental rebuild after touching one file takes 4.2 s (Mono processor), 4.0 s (Rav processor), 4.0 s (a Mono test),
2.9 s (a Rav test) and 2.3 s (a framework test), two runs each; relinking the executable alone takes 1.7 s and a
no-op build 0.1 s. Compiling the touched file dominates; a split could save at most part of the 1.7 s link, and a
product-code edit would relink that product's, the compat and the Audio Lab executables.

**Evidence (2 October 2026, Debug `dev`):** steps 1–3 pass `-L quality` (22), `-L presets` (27), `-L compat` (11),
`-L ui` (43), `-L audio-lab` (72), `-L mono` (292) and the Rav/Glimmer `[processor]` cases (41); the Mono reference
render matched exactly at zero tolerance after step 2; pluginval strictness 10 (without GUI tests) passed for the
Rav, Glimmer and Mono VST3s. auval was not run (it needs an installed component).

**Evidence for steps 4, 6 and 7 (3 October 2026, Debug `dev`):** `./scripts/test.sh`, the full suite with slow tests,
reference renders and the time-budget check, passed 484/484 after step 6; `-L plugin-support` 5/5, each behaviour
shown to make a case fail when removed; `scripts/build-dev.sh` built all nine wrappers and the `audio-lab` preset built
every Audio Lab tool; pluginval strictness 10 (without GUI tests) passed for the Rav, Glimmer and Mono VST3s. Step 7
changed documentation only. After the review follow-ups (the delete-the-selected-preset rule moved into
`PresetSession::removeUserPreset` with tests, a legacy-key-only restore test, a Rav stage-order restore test), the full
suite passed 485/485, all nine wrappers and the Audio Lab tools built, and pluginval passed again for all three VST3s.

**Step 9 evidence (3 October 2026, Debug `dev`):** the full suite passed 498/498 with the time-budget check after the
review follow-ups (every linear ramp in double through `dsp::LinearRamp`); all nine wrappers and the Audio Lab tools
built; pluginval strictness 10 (without GUI tests) passed for the three VST3s. Mono and Rav references were recaptured
(approved; Glimmer's unchanged); Rav cost 8-16 % more at 16x in Release; step 10 cached its coefficients, recovering the fuzz modes with static controls (RAV_VALIDATION.md). **Step 10 (3 October 2026):** Mono's noise now matches 1x up to 2 kHz at every quality (Thomas chose the host-rate hold and, the same
day, no gain for its top-octave roll-off), and Rav's coefficients are cached
(bit-identical; KOBBER_VALIDATION.md, RAV_VALIDATION.md). The full suite passed 500/500 (after the review follow-ups) with the
time-budget check; all nine wrappers and the Audio Lab tools built; pluginval strictness 10 (without GUI tests) passed
for the three VST3s.

**Step 8 open items** were resolved by step 9 (evidence in KOBBER_VALIDATION.md, 3 October 2026): the full suite passed
496/496 with the time-budget check, all nine wrappers and the Audio Lab tools built, and pluginval strictness 10
(without GUI tests) passed for the three VST3s.

**Blockers:** none.

**Open release gates (deferred by Thomas, pre-release, 2 October 2026):** host checks in Ableton Live 12 for VST3 and
AUv2: a quality change during playback updates host latency; the host program list, program switching and project
reopen restore the selected preset; the preset browser in Standalone, VST3 and AUv2. `auval` for the three AUv2
components needs them installed, which needs Thomas's approval. Check the Rav and Mono Settings pop-over visually at 1x and 2x scale, and listen to Mono at 16x and after the
step 9 precision change. Also check in VST3 that choosing the first factory
program while a user preset (program 0) is selected still loads it: JUCE's VST3 wrapper skips `setCurrentProgram`
when the host's choice equals `getCurrentProgram()`. Until these run, validation of these steps is
incomplete.

## Consequences

- Shared host behaviour changes in one place and is tested once, with product tests covering the adapters.
- A quality change during playback is audible; the editors no longer show a pending quality.
- Saved Rav and Glimmer projects lose one metadata key; projects saved earlier still restore.
