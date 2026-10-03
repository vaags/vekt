#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
cmake --preset audio-lab-coupled
cmake --build build/audio-lab-coupled --target VektAudioLab
open -a "$(pwd)/build/audio-lab-coupled/tools/audio_lab/VektAudioLab.app"