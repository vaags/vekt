#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
source scripts/lib/watch.zsh

watch_paths=(
	"tools/audio_lab"
	"plugins/vekt_rav/Source"
	"plugins/vekt_rav/Resources"
	"plugins/vekt_glimmer"
	"plugins/vekt_mono"
	"framework"
	"CMakeLists.txt"
	"CMakePresets.json"
)

vekt_require_fswatch
cmake --preset audio-lab
printf 'Watching Audio Lab sources. Press Ctrl+C to stop.\n'
VEKT_WATCH_BUILD_COMMAND=(cmake --build --preset audio-lab --target VektRavAudioLab)
vekt_run_watch_build
vekt_watch 'Change detected, rebuilding Vekt Audio Lab...' "${watch_paths[@]}"
