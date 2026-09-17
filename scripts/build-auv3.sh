#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
command -v xcodebuild >/dev/null || { print -u2 "Full Xcode is required"; exit 69; }
cmake --preset xcode
cmake --build --preset xcode-debug --target VektRav_AUv3
