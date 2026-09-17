#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
vst3_path=${1:-build/dev/plugins/vekt_rav/VektRav_artefacts/Debug/VST3/Vekt Rav.vst3}
auv3_path=${2:-build/xcode/plugins/vekt_rav/VektRav_artefacts/Debug/AUv3/Vekt Rav.appex}
packaging/macos/validate-plugins.sh "$vst3_path" "$auv3_path"
