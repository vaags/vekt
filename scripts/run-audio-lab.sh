#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
# Release by default: the Debug DSP is about 9x slower and drops out long before the CPU is busy.
# Pass --debug for an unoptimized build with assertions.
preset=audio-lab-release
if [[ "${1:-}" == "--debug" ]]; then
	preset=audio-lab
fi
cmake --preset "$preset"
cmake --build --preset "$preset" --target VektAudioLab
open -a "$(pwd)/build/$preset/tools/audio_lab/VektAudioLab.app"
