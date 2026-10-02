# macOS Release Gates

Build AUv2 components and matching Release VST3 bundles:

```sh
zsh scripts/build-au.sh --release
cmake --build --preset release --target VektRav_VST3 VektGlimmer_VST3 VektMono_VST3
```

AU components are at
`build/release/plugins/vekt_<product>/<Target>_artefacts/Release/AU/<Product Name>.component`.
Each AU build is ad-hoc sealed after linking with JUCE's bundle-signing check, as
JUCE already does for VST3, so strict signature verification can pass. It uses no
identity and is not release signing.
After separately approved installation in `~/Library/Audio/Plug-Ins/Components`
and registration, run from the repository root:

```sh
zsh scripts/validate-release.sh rav
zsh scripts/validate-release.sh glimmer
zsh scripts/validate-release.sh mono
```

Supply optional VST3/AU paths together to override Release defaults. The runner
requires a matching installed component, verifies signatures, and runs pluginval
strictness 10 and the product's registered `auval` identity. It waits up to 30
seconds (`VEKT_AU_REGISTRATION_TIMEOUT`) for a newly installed component to
register before failing:

| Product | AU tuple |
| --- | --- |
| Rav | `aufx Ravv Vekt` |
| Glimmer | `aufx Glmr Vekt` |
| Mono | `aumu Mono Vekt` |

Pluginval is resolved from PATH or
`/Applications/pluginval.app/Contents/MacOS/pluginval`. Missing tools and failed
checks return nonzero. AU validators address the registry, not a supplied path;
confirm the host-loaded build separately, including after replacing a component.

AU plists keep JUCE's default `resourceUsage` (`network.client` and
`temporary-exception.files.all.read-write`). JUCE can only suppress both together,
and sandboxed hosts need the file exception for desktop user presets.

Before release, test Logic Pro, GarageBand, and Ableton Live: state recall,
automation, factory/user presets, native editors/choosers, bypass and latency.
Verify desktop preset access in sandboxed hosts and Mono MIDI/Multicore workgroup
lifecycle. Per-product audio/performance acceptance remains required.

Installation/replacement, registration/cache changes, signing, notarization and
release require approval; apart from the ad-hoc seal above, build/validation
scripts do not perform these steps.
Signature integrity is not distribution trust. Run `notarize.sh` only on a
signed installer with `VEKT_SIGNING_IDENTITY` and `VEKT_NOTARY_PROFILE` configured
in the local keychain. Required skipped gates leave validation incomplete.
