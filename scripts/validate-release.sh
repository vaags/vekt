#!/bin/zsh
set -euo pipefail

cd "${0:A:h}/.."
if (( $# != 1 && $# != 3 )); then
	print -u2 "Usage: $0 <rav|glimmer|kobber|flint> [vst3-path au-component-path]"
	exit 64
fi
product=$1
case "$product" in
rav) target=Rav; product_name=Rav; folder=rav ;;
glimmer) target=Glimmer; product_name=Glimmer; folder=glimmer ;;
kobber) target=Kobber; product_name=Kobber; folder=kobber ;;
flint) target=Flint; product_name=Flint; folder=flint ;;
*) print -u2 "Unknown product: $product"; exit 64 ;;
esac

artifact_root="build/release/plugins/${folder}/${target}_artefacts/Release"
vst3_path=${2:-${artifact_root}/VST3/${product_name}.vst3}
au_path=${3:-${artifact_root}/AU/${product_name}.component}
exec /bin/zsh packaging/macos/validate-plugins.sh "$product" "$vst3_path" "$au_path"
