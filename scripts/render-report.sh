#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
cmake --preset audio-lab
cmake --build --preset audio-lab --target VektRender
build/audio-lab/tools/audio_lab/VektRender "$@"
