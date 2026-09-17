# Vekt

Vekt is a reusable C++20/JUCE framework for macOS audio effects. The first
reference effect is Vekt Rav with reusable DSP, state, and preset
infrastructure.

## Requirements

- macOS 27 or newer on Apple Silicon
- CMake 3.25 or newer
- Ninja for command-line development builds
- Full Xcode for AUv3 builds

Initialize dependencies after cloning:

```sh
git submodule update --init --recursive
```

Configure, build, and test the current development slice:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The `xcode` configure preset is reserved for AUv3-capable builds. It requires a
full Xcode installation selected through `xcode-select`; Apple Command Line
Tools alone are insufficient.

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
- Versioned project state and deterministic Rav preset catalogs
- Versioned sound-only JSON presets with factory/user catalogs and platform-aware storage
- 53 focused Catch2 tests covering DSP, modes, multiband routing, state, presets,
  latency, and tracking/offline quality selection

Standalone, VST3, and AUv3 development builds are validated locally. Host
registration, pluginval/auval, DAW smoke tests, signing/notarization, and
Apple M4 performance measurements remain release-time validation gates.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for module and real-time rules,
[docs/UI_UX.md](docs/UI_UX.md) for shared editor conventions, and
[docs/PRESET_UX.md](docs/PRESET_UX.md) for preset-specific interactions.

## License

Vekt is licensed under the GNU Affero General Public License version 3 only.
See [LICENSE](LICENSE). Third-party dependencies retain their own licenses; JUCE
licensing details are provided in `external/JUCE/LICENSE.md`.
