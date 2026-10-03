#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
source scripts/lib/watch.zsh

watch_paths=(
	"plugins/rav/Source"
	"plugins/rav/Resources"
	"plugins/glimmer"
	"plugins/kobber"
	"plugins/flint"
	"framework"
	"CMakeLists.txt"
	"CMakePresets.json"
)

vekt_require_fswatch
cmake --preset dev
printf 'Watching Vekt plugin sources. Press Ctrl+C to stop.\n'
VEKT_WATCH_BUILD_COMMAND=(cmake --build --preset dev --target Rav_Standalone Glimmer_Standalone Kobber_Standalone Flint_Standalone)
vekt_run_watch_build
vekt_watch 'Change detected, rebuilding Vekt standalone products...' "${watch_paths[@]}"
