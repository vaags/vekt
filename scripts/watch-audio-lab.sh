#!/bin/zsh

set -u

root="${0:A:h}/.."
cd "$root" || exit 1

build_dir="build/audio-lab"
watch_paths=(
	"tools/audio_lab"
	"plugins/vekt_rav/Source"
	"plugins/vekt_rav/Resources"
	"framework"
	"CMakeLists.txt"
)

configure()
{
	cmake -S . -B "$build_dir" -G Ninja \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DCMAKE_OSX_ARCHITECTURES=arm64 \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=27.0 \
		-DVEKT_BUILD_TESTS=OFF \
		-DVEKT_BUILD_AUDIO_LAB=ON
}

if [[ ! -f "$build_dir/build.ninja" ]]; then
	configure || exit $?
fi

build()
{
	cmake --build "$build_dir" --target VektRavAudioLab
	local build_result=$?
	if (( build_result != 0 )); then
		printf 'Build failed with exit code %d; continuing to watch.\n' "$build_result"
	fi
}

printf 'Watching Audio Lab sources. Press Ctrl+C to stop.\n'
build

while true; do
	fswatch --one-event --recursive "${watch_paths[@]}" >/dev/null
	printf '\nChange detected, rebuilding VektRav Audio Lab...\n'
	build
done
