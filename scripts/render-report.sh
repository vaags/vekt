#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
cmake -S . -B build/audio-lab -G Ninja \
	-DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_OSX_ARCHITECTURES=arm64 \
	-DCMAKE_OSX_DEPLOYMENT_TARGET=27.0 \
	-DVEKT_BUILD_TESTS=OFF \
	-DVEKT_BUILD_AUDIO_LAB=ON
cmake --build build/audio-lab --target VektRavRender
build/audio-lab/tools/audio_lab/VektRavRender "$@"
