# Vekt

Vekt is a reusable C++20/JUCE framework for macOS audio effects and instruments.
Vekt Rav, Vekt Glimmer, and Vekt Mono use its shared DSP, state, preset, and
native editor infrastructure.

## Requirements

- macOS 27 or newer on Apple Silicon
- CMake 3.25 or newer
- Ninja for command-line development builds
- Apple Command Line Tools for Ninja builds; full Xcode only for the Xcode preset

Initialize dependencies after cloning:

```sh
git submodule update --init --recursive
```

Configure, build, and test the current development slice:

```sh
./scripts/test.sh
```

Build the Standalone, VST3, and AUv2 development artifacts for all three products with:

```sh
./scripts/build-dev.sh
```

For AUv2 components only, use `zsh scripts/build-au.sh`; add `--release` for
Release builds.

Run a deterministic offline processor report without opening a DAW:

```sh
./scripts/render-report.sh --source sine --seconds 1 --mode 0
```

Render Glimmer alone or the fixed two-effect serial rack with:

```sh
./scripts/render-report.sh --product glimmer --source sine --seconds 1
./scripts/render-report.sh --rack rav,glimmer --source sine --seconds 1
./scripts/render-report.sh --product glimmer --param glimmer.cabinetModel=Drum --param glimmer.brake=true
```

Glimmer offers Classic, Drum and Wide speaker models, Brake, continuous Manual
speed and wet-only stereo Width. Its single-select model buttons follow RAV's
visual pattern without dragging or multi-selection. Click the current preset
name to open the shared browser; the adjacent arrows step through factory and
user presets. Six factory sounds cover organ, guitar, electric piano and pads.
`--param glimmer.id=value` accepts native units
and named choices, also in either rack order. See
[Glimmer validation](docs/GLIMMER_VALIDATION.md) for control semantics and gates.

For live listening without BlackHole or other routing tools, run the opt-in
Audio Lab:

```sh
./scripts/run-audio-lab.sh
```

It builds Release, so real-time load matches the plugins; pass `--debug` for an
unoptimized build with assertions (about 9x slower DSP, so expect dropouts).

To run the full test suite before launching the lab:

```sh
./scripts/test-and-run-audio-lab.sh
```

The same workflows are available in VS Code through **Tasks: Run Task** as
`Vekt: Run Audio Lab` and `Vekt: Test and Run Audio Lab`.

It generates test signals internally and sends them to the selected default
audio output. The lab hosts Rav and Glimmer in a serial chain; select either
editor tab and use the order control to audition both routing orders, then
explicitly arm output. Use headphones or safe monitoring when testing.

The optional `xcode` configure preset builds the same formats through Xcode.
It requires a full Xcode installation selected through `xcode-select`; Apple
Command Line Tools alone are sufficient for the normal Ninja workflows.

## Install In A DAW

For Ableton Live on macOS, use the VST3 build. After building with the `dev`
preset, install the bundle in the user VST3 directory:

```sh
./scripts/install-vst3.sh
```

In Ableton Live, open **Settings > Plug-Ins**, enable **Use VST3 Plug-In
System Folders**, then trigger a plug-in rescan. Find **Vekt Rav** under the
Audio Effects browser and drag it onto an audio track.

Development builds may be ad-hoc signed and can be rejected by Gatekeeper or
the host. For normal use, install a signed release bundle instead. AUv2
components support AU hosts such as Logic Pro, GarageBand, and Ableton Live;
VST3 remains an option in Live. Installation, replacing old copies, signing,
and rescanning require explicit approval in the development workflow.
See [macOS release gates](packaging/macos/README.md) for AU installation and
product-aware validation. No build script installs plugins automatically.

## Current Scope

- Pinned JUCE 9.0.2 dependency
- Six Rav processing modes: saturation, overdrive, distortion, fuzz, wavefold,
  and bitcrush
- Reusable three-band crossover with configurable low-mid and mid-high cutoffs
- Per-band low/mid/high wet controls followed by global dry/wet mixing
- Preallocated tracking profiles from Off through 4x IIR and offline profiles
  through 16x FIR
- Integer oversampling latency reporting
- Linear dry/wet mixing with dry-path latency alignment
- Vector-scalable Rav editor with mode, multiband, preset, bypass, and quality controls
- Vector-scalable Glimmer editor with Classic/Drum/Wide models, independent rotor
  mechanics, mic geometry, shelf tone controls, preamp, Mix, Width, Auto sensitivity,
  Brake and continuous Manual speed
- Versioned project state and deterministic Rav preset catalogs
- Versioned sound-only JSON presets with factory/user catalogs and platform-aware storage
- 53 focused Catch2 tests covering DSP, modes, multiband routing, state, presets,
  latency, and tracking/offline quality selection

Standalone, VST3, and AUv2 targets are available in the existing presets. Build
and test evidence is distinct from host registration, pluginval/auval, DAW
smoke tests, trusted signing/notarization, and Apple M4 performance measurements;
those remain separate release gates.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for module and real-time rules,
[docs/UI_UX.md](docs/UI_UX.md) for shared editor conventions, and
[docs/PRESET_UX.md](docs/PRESET_UX.md) for preset-specific interactions.
The [Claude development workflow](docs/DEVELOPMENT_WORKFLOW.md) defines planning,
scoped changes, review, and validation evidence for all three products.

## License

Vekt is licensed under the GNU Affero General Public License version 3 only.
See [LICENSE](LICENSE). Third-party dependencies retain their own licenses; JUCE
licensing details are provided in `external/JUCE/LICENSE.md`.
