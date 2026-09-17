#!/bin/zsh
set -euo pipefail

if (( $# != 2 )); then
	print -u2 "Usage: $0 /path/to/VektRav.vst3 /path/to/VektRav.appex"
	exit 64
fi

vst3_path=$1
auv3_path=$2
command -v pluginval >/dev/null || { print -u2 "pluginval is required"; exit 69; }
command -v auval >/dev/null || { print -u2 "auval is required"; exit 69; }

pluginval --validate "$vst3_path" --strictness-level 10
auval -v aufx Vsat Vekt
codesign --verify --deep --strict --verbose=2 "$vst3_path"
codesign --verify --deep --strict --verbose=2 "$auv3_path"
