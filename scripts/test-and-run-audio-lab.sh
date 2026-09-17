#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
./scripts/test.sh
./scripts/run-audio-lab.sh
