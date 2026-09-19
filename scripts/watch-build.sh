#!/bin/zsh

set -u

root="${0:A:h}/.."
cd "$root" || exit 1

watch_paths=(
	"plugins/vekt_rav/Source"
	"plugins/vekt_rav/Resources"
	"plugins/vekt_glimmer"
	"framework"
	"CMakeLists.txt"
	"CMakePresets.json"
)

printf 'Watching Vekt plugin sources. Press Ctrl+C to stop.\n'

while true; do
	fswatch --one-event --recursive "${watch_paths[@]}" >/dev/null
	printf '\nChange detected, rebuilding Vekt standalone products...\n'
	cmake --build --preset dev --target VektRav_Standalone VektGlimmer_Standalone
	build_result=$?
	if (( build_result != 0 )); then
		printf 'Build failed with exit code %d; continuing to watch.\n' "$build_result"
	fi
done
