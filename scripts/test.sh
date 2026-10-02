#!/bin/zsh
set -euo pipefail

# Usage: scripts/test.sh [--quick] [--opt]
# --quick skips tests tagged [slow] (long ladder/DSP sweeps).
# --opt   builds and runs the optimised dev-opt tests (about 5x faster), without the reference renders, which are
#         captured from and compared in the Debug build.
# Every run then checks test times against their budgets (scripts/check-test-budget.sh).
configure=dev
quick=
for option in "$@"; do
	case $option in
		--quick) quick=-quick ;;
		--opt) configure=dev-opt ;;
		*) print -u2 "Usage: $0 [--quick] [--opt]"; exit 64 ;;
	esac
done

cd "${0:A:h}/.."
cmake --preset "$configure"
cmake --build --preset "$configure" --target vekt_dsp_tests
junit="$PWD/build/$configure/Testing/junit.xml"
ctest --preset "$configure$quick" --parallel "$(sysctl -n hw.ncpu)" --output-junit "$junit"
scripts/check-test-budget.sh "build/$configure" "$junit"
