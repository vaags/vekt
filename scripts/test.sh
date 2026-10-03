#!/bin/zsh
set -euo pipefail

# Usage: scripts/test.sh [--quick] [--opt] [--t2]
# Tiers are in docs/VERIFICATION_SPEED.md. Without options this is the full Debug suite (release-level, T3).
# --quick skips tests tagged [slow] (long ladder/DSP sweeps).
# --opt   builds and runs the optimised dev-opt tests (about 5x faster), without the reference renders, which are
#         captured from and compared in the Debug build.
# --t2    the milestone check: the Debug suite without [slow] tests (assertions on, reference renders included), then
#         the [slow] tests optimised. About half the full Debug suite's time; Debug assertions in the slow tests
#         run only in the full suite.
# Every run then checks test times against their budgets (scripts/check-test-budget.sh).
configure=dev
quick=
milestone=0
for option in "$@"; do
	case $option in
		--quick) quick=-quick ;;
		--opt) configure=dev-opt ;;
		--t2) milestone=1 ;;
		*) print -u2 "Usage: $0 [--quick] [--opt] [--t2]"; exit 64 ;;
	esac
done
if (( milestone )) && [[ -n $quick || $configure != dev ]]; then
	print -u2 -- "--t2 chooses its own builds; it does not combine with --quick or --opt"
	exit 64
fi

cd "${0:A:h}/.."
jobs=$(sysctl -n hw.ncpu)
if (( milestone )); then
	for preset in dev dev-opt; do
		cmake --preset "$preset" > /dev/null
		cmake --build --preset "$preset" --target vekt_dsp_tests
	done
	# Both parts always run, so one part's failure or budget overrun does not hide the other's results.
	failed=()
	junit="$PWD/build/dev/Testing/junit.xml"
	ctest --preset dev-quick --parallel "$jobs" --output-junit "$junit" || failed+=("Debug tests")
	scripts/check-test-budget.sh build/dev "$junit" || failed+=("Debug budgets")
	junit="$PWD/build/dev-opt/Testing/junit-slow.xml"
	ctest --preset dev-opt -L '^slow$' --parallel "$jobs" --output-junit "$junit" --no-tests=error || failed+=("slow tests")
	scripts/check-test-budget.sh build/dev-opt "$junit" || failed+=("slow budgets")
	if (( ${#failed} > 0 )); then
		print -u2 "\nT2 FAILED: ${(j:, :)failed}"
		exit 1
	fi
	print "\nT2 PASSED"
	exit 0
fi

cmake --preset "$configure"
cmake --build --preset "$configure" --target vekt_dsp_tests
junit="$PWD/build/$configure/Testing/junit.xml"
ctest --preset "$configure$quick" --parallel "$jobs" --output-junit "$junit"
scripts/check-test-budget.sh "build/$configure" "$junit"
