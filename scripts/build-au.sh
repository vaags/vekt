#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
if (( $# > 1 )); then
	print -u2 "Usage: $0 [--release]"
	exit 64
fi
case "${1:-}" in
"") preset=dev ;;
--release) preset=release ;;
--help|-h) print "Usage: $0 [--release]"; exit 0 ;;
*) print -u2 "Usage: $0 [--release]"; exit 64 ;;
esac

cmake --preset "$preset"
cmake --build --preset "$preset" --target \
	Rav_AU Glimmer_AU VektMono_AU Flint_AU
