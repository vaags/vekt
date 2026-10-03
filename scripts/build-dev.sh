#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
cmake --preset dev
cmake --build --preset dev --target \
	VektRav_Standalone VektRav_VST3 VektRav_AU \
	VektGlimmer_Standalone VektGlimmer_VST3 VektGlimmer_AU \
	VektMono_Standalone VektMono_VST3 VektMono_AU

scripts/verify-bundles.sh
