#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
cmake --preset dev
cmake --build --preset dev --target \
	Rav_Standalone Rav_VST3 Rav_AU \
	Glimmer_Standalone Glimmer_VST3 Glimmer_AU \
	VektMono_Standalone VektMono_VST3 VektMono_AU \
	Flint_Standalone Flint_VST3 Flint_AU

scripts/verify-bundles.sh
