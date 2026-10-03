#!/bin/zsh
set -euo pipefail

# Usage: scripts/check-test-budget.sh <build-dir> <junit.xml>
# Fails when a test in the JUnit report (ctest --output-junit) ran longer than its budget: 3 s for an always-run test,
# 60 s for one labelled slow. Budgets are wall-clock seconds in a parallel run of the dev (Debug) build; dev-opt meets
# them more easily. A test may exceed them only with an allowance in tests/test-time-budget.txt. A test over budget is
# re-timed alone before it fails the check, since parallel load (other builds, other sessions) can slow any test: it
# fails only if alone it still takes more than two thirds of its budget, the headroom a parallel run needs.
always_budget=3
slow_budget=60

(( $# == 2 )) || { print -u2 "Usage: $0 <build-dir> <junit.xml>"; exit 64; }
build=$1
junit=$2
allowances="${0:A:h}/../tests/test-time-budget.txt"
[[ -f $junit ]] || { print -u2 "No JUnit report at $junit"; exit 66; }

typeset -A allowed slow
while IFS= read -r line; do
	[[ -z $line || $line == \#* ]] && continue
	rest=${line#* | }
	allowed[${rest%% | *}]=${line%% | *}
done < "$allowances"
ctest --test-dir "$build" -N -L '^slow$' | sed -n 's/^ *Test *#[0-9]*: //p' | while IFS= read -r name; do slow[$name]=1; done

overruns=()
grep -o '<testcase name="[^"]*"[^>]* time="[^"]*"' "$junit" \
	| sed -e 's/^<testcase name="\([^"]*\)".* time="\([^"]*\)"$/\2	\1/' \
		-e "s/&apos;/'/g" -e 's/&quot;/"/g' -e 's/&lt;/</g' -e 's/&gt;/>/g' -e 's/&amp;/\&/g' \
	| while IFS=$'\t' read -r seconds name; do
		budget=$always_budget kind=always-run
		(( ${+slow[$name]} )) && budget=$slow_budget kind=slow
		(( ${+allowed[$name]} )) && budget=${allowed[$name]} kind=allowance
		if (( seconds > budget )); then
			overruns+=("$seconds"$'\t'"$budget"$'\t'"$kind"$'\t'"$name")
		fi
	done

# Seconds the named test takes run alone, or nothing if it could not be selected.
time_alone()
{
	local pattern report
	pattern=$(python3 -c 'import re, sys; print("^" + re.sub(r"([][.^$*+?(){}|\\])", r"\\\1", sys.argv[1]) + "$")' "$1")
	report=$(mktemp "${TMPDIR:-/tmp}/vekt-budget.XXXXXX")
	ctest --test-dir "$build" -R "$pattern" --output-junit "$report" > /dev/null 2>&1 || true
	grep -o '<testcase name="[^"]*"[^>]* time="[^"]*"' "$report" | head -1 | sed 's/.* time="\([^"]*\)"$/\1/'
	rm -f "$report"
}

failures=0
for overrun in $overruns; do
	IFS=$'\t' read -r seconds budget kind name <<< "$overrun"
	alone=$(time_alone "$name")
	if [[ -n $alone ]] && (( alone * 3 <= budget * 2 )); then
		printf 'Over budget under load, not alone: %.1f s > %s s (%s), alone %.1f s: %s\n' "$seconds" "$budget" "$kind" "$alone" "$name"
	else
		printf 'OVER BUDGET: %.1f s > %s s (%s), alone %s: %s\n' "$seconds" "$budget" "$kind" \
			"$( [[ -n $alone ]] && printf '%.1f s' "$alone" || print 'not re-timed' )" "$name"
		failures=$(( failures + 1 ))
	fi
done

if (( failures > 0 )); then
	print "\n$failures test(s) over budget. Render at the lowest rate and length that exercises the behaviour, tag a"
	print "necessary long sweep [slow] or a measurement [.], or add a reasoned allowance (docs/VERIFICATION_SPEED.md)."
	exit 1
fi
print "Test time budget: PASS (always-run <= ${always_budget} s, slow <= ${slow_budget} s, plus allowances)"
