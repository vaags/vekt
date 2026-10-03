#!/bin/zsh
set -euo pipefail

# Usage: scripts/test-affected.sh [--dry-run] [--base <rev>] [path ...]
# The T1 check (docs/VERIFICATION_SPEED.md): the tests owning the changed files, run optimised (dev-opt, slow tests
# included), plus the compatibility checks in Debug, where the reference renders belong. Changed files are the given
# paths, or everything differing from <rev> (default HEAD) including untracked files. A path the map does not know,
# or a build-wide one (CMake, JUCE), selects the full suite. --dry-run prints the selection and its test counts only.
# T1 is not the milestone check: Debug assertions only run in the full Debug suite (scripts/test.sh).
dry_run=0
base=HEAD
changed=()
while (( $# > 0 )); do
	case $1 in
		--dry-run) dry_run=1 ;;
		--base) base=$2; shift ;;
		-h|--help) sed -n '4,10p' "$0"; exit 0 ;;
		*) changed+=("$1") ;;
	esac
	shift
done

cd "${0:A:h}/.."
if (( ${#changed} == 0 )); then
	changed=(${(f)"$(git diff --name-only "$base"; git ls-files --others --exclude-standard)"})
fi

products=(rav glimmer mono flint)
typeset -A labels
full=0
add() { for label in "$@"; do labels[$label]=1; done }

owner_pattern='rav|glimmer|mono|flint|dsp|ui|presets|state|plugin-support|compat|audio-lab|audio-analysis|framework'

# The owner tags of the registered (not hidden) test cases in the given test files, read across line breaks; the tag
# policy guarantees every registered case one.
test_file_labels() {
	(( $# > 0 )) || return 0
	perl -0777 -ne 'while (/\b(?:TEST_CASE|SCENARIO|TEMPLATE_TEST_CASE|TEMPLATE_PRODUCT_TEST_CASE)\(\s*"[^"]*"\s*,\s*"([^"]*)"|\bTEST_CASE_METHOD\(\s*[\w:<>]+\s*,\s*"[^"]*"\s*,\s*"([^"]*)"/g) { my $t = $1 // $2; print "$t\n" unless $t =~ /\[\.|\[!hide\]/ }' "$@" \
		| grep -oE '\[[a-z0-9-]+\]' | tr -d '[]' | sort -u | grep -xE "$owner_pattern" || true
}

# The owner labels of the test files that use a product or framework module (its namespace or its headers), so a
# change also selects, for example, the preset tests that construct a Rav processor. Products do not depend on each
# other, so for a product only the framework labels of those files are added (a shared editor test file's [mono] cases
# are not Rav's).
add_users() {
	local users=(${(f)"$(grep -rlE "vekt::$1::|<vekt/$1/" tests --include='*.cpp' --include='*.h' --include='*.mm' || true)"})
	(( ${#users} > 0 )) || return 0
	local owners=(${(f)"$(test_file_labels $users)"})
	if (( ${products[(Ie)$1]} )); then
		owners=(${owners:|products})
	fi
	add $owners
}

# (Not "path": in zsh that is the PATH array.)
for file in $changed; do
	case $file in
		''|docs/*|*.md|.claude/*|.editorconfig|.clang-format|.gitignore) ;;
		plugins/vekt_rav/*) add rav; add_users rav ;;
		plugins/vekt_glimmer/*) add glimmer; add_users glimmer ;;
		plugins/vekt_mono/*) add mono; add_users mono ;;
		plugins/flint/*) add flint; add_users flint ;;
		# Framework modules select their own tests, the modules that link them (framework/*/CMakeLists.txt), every
		# product, and every test file using them.
		framework/dsp/*) add dsp ui plugin-support $products; add_users dsp; add_users ui; add_users plugin_support ;;
		framework/ui/*) add ui $products; add_users ui; add_users preset_ui ;;
		framework/preset_ui/*) add ui $products; add_users preset_ui ;;
		framework/presets/*)
			add presets plugin-support $products; add_users presets; add_users preset_ui; add_users plugin_support ;;
		framework/state/*) add state presets plugin-support $products; add_users state; add_users plugin_support ;;
		framework/plugin_support/*) add plugin-support $products; add_users plugin_support ;;
		framework/audio_analysis/*) add audio-analysis audio-lab; add_users audio_analysis ;;
		tools/audio_lab/*) add audio-lab ;;
		tests/cmake/factory_presets/*) add presets scripts ;;
		tests/cmake/*|scripts/*|packaging/*|.pre-commit-config.yaml|.clang-tidy) add scripts ;;
		tests/fixtures/*) add compat ;;
		tests/*.cpp|tests/*/*.cpp|tests/*/*.h|tests/*.h|tests/*.mm|tests/*/*.mm)
			owners=()
			[[ -f $file ]] && owners=(${(f)"$(test_file_labels "$file")"})
			if (( ${#owners} > 0 )); then add $owners; else full=1; fi ;; # deleted, or hidden cases only
		*) full=1 ;; # CMake, presets, JUCE, external, anything unmapped
	esac
done

if (( full )); then
	selection="full suite"
	optimised=()
elif (( ${#labels} == 0 )); then
	print "No tests own the changed files (documentation or configuration only)."
	exit 0
else
	keys=(${(ok)labels})
	regex="^(${(j:|:)keys})\$"
	selection="labels ${(j:, :)keys}"
	optimised=(-L "$regex")
fi
print "T1 selection: $selection, plus compat in Debug"

if (( dry_run )); then
	cmake --build --preset dev-opt --target vekt_dsp_tests > /dev/null
	printf '  dev-opt: '; ctest --preset dev-opt -N ${optimised[@]} | tail -1
	printf '  dev (compat): '; ctest --preset dev -N -L '^compat$' | tail -1
	exit 0
fi

cmake --build --preset dev-opt --target vekt_dsp_tests
ctest --preset dev-opt ${optimised[@]} --no-tests=error
cmake --build --preset dev --target vekt_dsp_tests
ctest --preset dev -L '^compat$' --no-tests=error
