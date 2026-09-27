#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."

output_directory="${1:-/tmp/vekt-mono-render}"
mkdir -p "$output_directory"

cmake --preset audio-lab-release
cmake --build --preset audio-lab-release --target VektMonoRender VektLadderPrototype

renderer="build/audio-lab-release/tools/audio_lab/VektMonoRender"
for fixture in filter-sweep envelope; do
	"$renderer" \
		--fixture "$fixture" \
		--sample-rate 48000 \
		--block-size 127 \
		--seed 1299148399 \
		--wav "$output_directory/$fixture.wav" \
		--report "$output_directory/$fixture.json"
done

build/audio-lab-release/tools/audio_lab/VektLadderPrototype \
	--sample-rate 48000 \
	--block-size 127 \
	--wav "$output_directory/ladder-candidate.wav" \
	--report "$output_directory/ladder-candidate.json"

printf 'Mono renders written to %s\n' "$output_directory"
