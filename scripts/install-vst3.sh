#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
cmake --preset dev
cmake --build --preset dev --target VektRav_VST3

source_bundle="build/dev/plugins/vekt_rav/VektRav_artefacts/Debug/VST3/Vekt Rav.vst3"
destination="$HOME/Library/Audio/Plug-Ins/VST3/Vekt Rav.vst3"
[[ -d "$source_bundle" ]] || { print -u2 "Built VST3 bundle not found: $source_bundle"; exit 1; }
mkdir -p "${destination:h}"
rm -rf "$destination"
ditto "$source_bundle" "$destination"
print "Installed $destination"
