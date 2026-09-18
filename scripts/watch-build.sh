#!/bin/zsh

set -u

root="${0:A:h}/.."
cd "$root" || exit 1

watch_paths=(
	"plugins/vekt_rav/Source"
	"plugins/vekt_rav/Resources"
	"framework"
	"CMakeLists.txt"
	"CMakePresets.json"
)

printf 'Watching Vekt Rav sources. Press Ctrl+C to stop.\n'

while true; do
	fswatch --one-event --recursive "${watch_paths[@]}" >/dev/null
	printf '\nChange detected, rebuilding Vekt Rav Standalone...\n'
	cmake --build --preset dev --target VektRav_Standalone
	build_result=$?
	if (( build_result != 0 )); then
		printf 'Build failed with exit code %d; continuing to watch.\n' "$build_result"
	fi
done
