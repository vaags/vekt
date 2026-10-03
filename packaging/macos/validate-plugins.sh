#!/bin/zsh
set -euo pipefail

if (( $# != 3 )); then
	print -u2 "Usage: $0 <rav|glimmer|kobber|flint> /path/to/product.vst3 /path/to/product.component"
	exit 64
fi

product=$1
# The maker identity from the root CMakeLists.txt (docs/PRODUCT_NAMING.md).
au_manufacturer=Tava
bundle_id_prefix=com.thomasvaags
case "$product" in
rav) au_type=aufx; au_subtype=Ravv; au_category=Effects ;;
glimmer) au_type=aufx; au_subtype=Glmr; au_category=Effects ;;
kobber) au_type=aumu; au_subtype=Kobr; au_category=Synths ;;
flint) au_type=aumu; au_subtype=Flnt; au_category=Synths ;;
*) print -u2 "Unknown product: $product"; exit 64 ;;
esac
vst3_path=${2:A}
au_path=${3:A}

fail()
{
	print -u2 "FAIL: $*"
	exit 1
}

read_plist()
{
	plutil -extract "$2" raw -o - "$1/Contents/Info.plist"
}

[[ "$vst3_path" == *.vst3 && -d "$vst3_path" ]] || fail "VST3 bundle not found: $vst3_path"
[[ "$au_path" == *.component && -d "$au_path" ]] || fail "AU component not found: $au_path"
for bundle in "$vst3_path" "$au_path"; do
	[[ "$(read_plist "$bundle" CFBundleIdentifier)" == "${bundle_id_prefix}.${product}" ]] \
		|| fail "Bundle product mismatch: $bundle"
done
[[ "$(read_plist "$au_path" AudioComponents.0.type)" == "$au_type" \
	&& "$(read_plist "$au_path" AudioComponents.0.subtype)" == "$au_subtype" \
	&& "$(read_plist "$au_path" AudioComponents.0.manufacturer)" == "$au_manufacturer" ]] \
	|| fail "AU component identity mismatch: $au_path"

installed_components=()
for candidate in "$HOME/Library/Audio/Plug-Ins/Components/"*.component(N) \
	/Library/Audio/Plug-Ins/Components/*.component(N); do
	if [[ "$(read_plist "$candidate" AudioComponents.0.type 2>/dev/null || true)" == "$au_type" \
		&& "$(read_plist "$candidate" AudioComponents.0.subtype 2>/dev/null || true)" == "$au_subtype" \
		&& "$(read_plist "$candidate" AudioComponents.0.manufacturer 2>/dev/null || true)" == "$au_manufacturer" ]]; then
		installed_components+=("$candidate")
	fi
done
(( ${#installed_components} == 1 )) \
	|| fail "Expected one installed $product AU component; found ${#installed_components}. Installation/registration requires separate approval."
installed_component=${installed_components[1]}
/usr/bin/diff -qr "$au_path" "$installed_component" \
	|| fail "Installed AU component differs from supplied build: $installed_component"

pluginval_tool=$(command -v pluginval || true)
if [[ -z "$pluginval_tool" && -x /Applications/pluginval.app/Contents/MacOS/pluginval ]]; then
	pluginval_tool=/Applications/pluginval.app/Contents/MacOS/pluginval
fi
[[ -n "$pluginval_tool" ]] || { print -u2 "SKIP: pluginval is required"; exit 69; }
command -v auval >/dev/null || { print -u2 "SKIP: auval is required"; exit 69; }
command -v codesign >/dev/null || { print -u2 "SKIP: codesign is required"; exit 69; }

print -r -- "Product: $product"
print -r -- "VST3: $vst3_path"
print -r -- "AU build: $au_path"
print -r -- "Installed matching AU: $installed_component"
codesign --verify --deep --strict --verbose=2 "$vst3_path"
codesign --verify --deep --strict --verbose=2 "$au_path"
"$pluginval_tool" --validate "$vst3_path" --strictness-level 10

# Registration of a newly installed component can lag by several seconds.
registration_timeout=${VEKT_AU_REGISTRATION_TIMEOUT:-30}
for (( waited = 0; ; ++waited )); do
	registered=("${(@f)$(auval -a 2>/dev/null || true)}")
	(( ${registered[(I)${au_type} ${au_subtype} ${au_manufacturer} *]} )) && break
	(( waited < registration_timeout )) \
		|| fail "$product AU ($au_type $au_subtype $au_manufacturer) not registered after ${registration_timeout}s. Registration/cache changes require separate approval."
	/bin/sleep 1
done
"$pluginval_tool" --validate "AudioUnit:${au_category}/${au_type},${au_subtype},${au_manufacturer}" --strictness-level 10
auval -v "$au_type" "$au_subtype" "$au_manufacturer"
print "PASS: $product validators and signature integrity; manual host/distribution gates remain separate."
