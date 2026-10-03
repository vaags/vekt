#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
source scripts/lib/watch.zsh

watch_paths=(
	"tools/audio_lab"
	"plugins/vekt_rav"
	"plugins/vekt_glimmer"
	"plugins/vekt_mono"
	"plugins/flint"
	"framework"
	"CMakeLists.txt"
	"CMakePresets.json"
)

# The same build as run-audio-lab.sh: Release, or --debug.
preset=audio-lab-release
if [[ "${1:-}" == "--debug" ]]; then
	preset=audio-lab
fi

vekt_require_fswatch
cmake --preset "$preset"
printf 'Watching Audio Lab sources. Press Ctrl+C to stop.\n'
VEKT_WATCH_BUILD_COMMAND=(cmake --build --preset "$preset" --target VektRavAudioLab)
vekt_run_watch_build
vekt_watch 'Change detected, rebuilding Vekt Audio Lab...' "${watch_paths[@]}"
