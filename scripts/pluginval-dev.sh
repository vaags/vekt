#!/bin/zsh
set -uo pipefail

# Usage: scripts/pluginval-dev.sh [--gui] [rav|glimmer|kobber|flint ...]
# pluginval at strictness 10 on each product's development (Debug) VST3, all products in parallel, each against a
# snapshot copy of its bundle so a rebuild meanwhile cannot change what is validated. GUI tests are skipped unless
# --gui is given (they need a display session). Logs: build/dev/pluginval/<product>.log. Exits nonzero if any fails.
# Builds the VST3s first. Release validation with installed AUs is scripts/validate-release.sh.
gui=0
products=()
for option in "$@"; do
	case $option in
		--gui) gui=1 ;;
		rav|glimmer|kobber|flint) products+=("$option") ;;
		*) print -u2 "Usage: $0 [--gui] [rav|glimmer|kobber|flint ...]"; exit 64 ;;
	esac
done
(( ${#products} == 0 )) && products=(rav glimmer kobber flint)

cd "${0:A:h}/.."
pluginval=$(command -v pluginval || print /Applications/pluginval.app/Contents/MacOS/pluginval)
[[ -x $pluginval ]] || { print -u2 "pluginval not found"; exit 69; }

typeset -A names=(rav Rav glimmer Glimmer kobber Kobber flint Flint)
typeset -A targets=(rav Rav glimmer Glimmer kobber Kobber flint Flint)
typeset -A folders=(rav rav glimmer glimmer kobber kobber flint flint)
build_targets=()
for product in $products; do build_targets+=("${targets[$product]}_VST3"); done
build_log=$(mktemp "${TMPDIR:-/tmp}/vekt-pluginval-build.XXXXXX")
if ! cmake --build --preset dev --target $build_targets > "$build_log" 2>&1; then
	tail -40 "$build_log" >&2
	rm -f "$build_log"
	exit 1
fi
rm -f "$build_log"

logs=build/dev/pluginval
snapshots=$(mktemp -d "${TMPDIR:-/tmp}/vekt-pluginval.XXXXXX")
mkdir -p "$logs"
pids=()
# On any exit, stop validators still running and remove the snapshots.
trap 'for pid in $pids; do kill $pid 2>/dev/null; done; rm -rf "$snapshots"' EXIT
for product in $products; do
	bundle="build/dev/plugins/${folders[$product]}/${targets[$product]}_artefacts/Debug/VST3/${names[$product]}.vst3"
	[[ -d $bundle ]] || { print -u2 "Missing $bundle"; exit 66; }
	cp -R "$bundle" "$snapshots/"
	arguments=(--strictness-level 10 --validate-in-process)
	(( gui )) || arguments+=(--skip-gui-tests)
	"$pluginval" $arguments --validate "$snapshots/${names[$product]}.vst3" > "$logs/$product.log" 2>&1 &
	pids+=($!)
done

failed=0
for index in {1..${#products}}; do
	product=${products[$index]}
	if wait ${pids[$index]} && grep -q '^SUCCESS' "$logs/$product.log"; then
		print "$product: SUCCESS (strictness 10$( (( gui )) || print ', no GUI tests'))"
	else
		print "$product: FAILED, see $logs/$product.log"
		failed=1
	fi
done
exit $failed
