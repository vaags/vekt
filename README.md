# Vekt

Vekt is a reusable C++20/JUCE framework for macOS audio effects. The first
reference effect is a `tanh` saturator with reusable DSP, state, and preset
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
- Reusable centered `tanh` transfer stage
- Preallocated Off/2x/4x minimum- and linear-phase oversampling paths
- Integer oversampling latency reporting
- Linear dry/wet mixing with dry-path latency alignment
- Versioned project state with legacy migration
- Versioned sound-only JSON presets, embedded factory catalog, and filesystem repository
- Focused Catch2 tests

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for module and real-time rules.

## License

Vekt is licensed under the GNU Affero General Public License version 3 only.
See [LICENSE](LICENSE). Third-party dependencies retain their own licenses; JUCE
licensing details are provided in `external/JUCE/LICENSE.md`.
