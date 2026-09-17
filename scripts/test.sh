#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
cmake --preset dev
cmake --build --preset dev --target vekt_dsp_tests
ctest --preset dev --output-on-failure
