#!/bin/zsh
set -euo pipefail

# Usage: scripts/test.sh [--quick]
# --quick skips tests tagged [slow] (long ladder/DSP sweeps).
preset=dev
[[ "${1:-}" == "--quick" ]] && preset=dev-quick

cd "${0:A:h}/.."
cmake --preset dev
cmake --build --preset dev --target vekt_dsp_tests
ctest --preset "$preset" --parallel "$(sysctl -n hw.ncpu)"
