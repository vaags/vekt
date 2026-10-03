#!/bin/zsh
set -uo pipefail

# Usage: scripts/verify-bundles.sh [--release] [rav|glimmer|mono ...]
# Strict code-signature check (codesign --verify --deep --strict) of each product's built Standalone, VST3 and AU
# bundles in build/dev (Debug) or build/release (Release). A format that was not built is reported as SKIP; an invalid
# seal fails. Exits nonzero if any bundle fails or none was found. Run after building, before installing or validating.
preset=dev
config=Debug
products=()
for option in "$@"; do
	case $option in
		--release) preset=release; config=Release ;;
		rav|glimmer|mono) products+=("$option") ;;
		*) print -u2 "Usage: $0 [--release] [rav|glimmer|mono ...]"; exit 64 ;;
	esac
done
(( ${#products} == 0 )) && products=(rav glimmer mono)

cd "${0:A:h}/.."
typeset -A names=(rav Rav glimmer Glimmer mono Mono)
typeset -A targets=(rav VektRav glimmer VektGlimmer mono VektMono)
checked=0
failed=0
for product in $products; do
	root="build/$preset/plugins/vekt_$product/${targets[$product]}_artefacts/$config"
	for bundle in "$root/Standalone/${names[$product]}.app" "$root/VST3/${names[$product]}.vst3" \
		"$root/AU/${names[$product]}.component"; do
		if [[ ! -d $bundle ]]; then
			print -r -- "SKIP: not built: $bundle"
			continue
		fi
		(( ++checked ))
		if codesign --verify --deep --strict "$bundle" 2>/dev/null; then
			print -r -- "PASS: $bundle"
		else
			print -r -- "FAIL: invalid seal: $bundle"
			codesign --verify --deep --strict "$bundle" 2>&1 | sed 's/^/  /' >&2
			(( ++failed ))
		fi
	done
done
(( checked > 0 )) || { print -u2 "FAIL: no built bundles found in build/$preset"; exit 1; }
(( failed == 0 )) || exit 1
