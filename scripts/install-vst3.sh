#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
product=${1:-rav}

case "$product" in
rav)
	target=VektRav_VST3
	source_bundle="build/dev/plugins/vekt_rav/VektRav_artefacts/Debug/VST3/Rav.vst3"
	destination="$HOME/Library/Audio/Plug-Ins/VST3/Rav.vst3"
	;;
glimmer)
	target=VektGlimmer_VST3
	source_bundle="build/dev/plugins/vekt_glimmer/VektGlimmer_artefacts/Debug/VST3/Glimmer.vst3"
	destination="$HOME/Library/Audio/Plug-Ins/VST3/Glimmer.vst3"
	;;
mono)
	target=VektMono_VST3
	source_bundle="build/dev/plugins/vekt_mono/VektMono_artefacts/Debug/VST3/Mono.vst3"
	destination="$HOME/Library/Audio/Plug-Ins/VST3/Mono.vst3"
	;;
flint)
	target=Flint_VST3
	source_bundle="build/dev/plugins/flint/Flint_artefacts/Debug/VST3/Flint.vst3"
	destination="$HOME/Library/Audio/Plug-Ins/VST3/Flint.vst3"
	;;
*)
	print -u2 "Usage: $0 [rav|glimmer|mono|flint]"
	exit 64
	;;
esac

cmake --preset dev
cmake --build --preset dev --target "$target"

[[ -d "$source_bundle" ]] || { print -u2 "Built VST3 bundle not found: $source_bundle"; exit 1; }
mkdir -p "${destination:h}"
rm -rf "$destination"
ditto "$source_bundle" "$destination"
codesign --force --deep --sign - "$destination"
print "Installed $destination"
