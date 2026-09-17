#!/bin/zsh
set -euo pipefail

: "${VEKT_SIGNING_IDENTITY:?Set VEKT_SIGNING_IDENTITY to a Developer ID Application identity}"
: "${VEKT_NOTARY_PROFILE:?Set VEKT_NOTARY_PROFILE to a notarytool keychain profile}"

if (( $# != 1 )); then
	print -u2 "Usage: $0 /path/to/VektSaturator.pkg"
	exit 64
fi

package_path=$1
command -v xcrun >/dev/null || { print -u2 "Xcode command-line tools are required"; exit 69; }
codesign --verify --deep --strict --verbose=2 "$package_path"
xcrun notarytool submit "$package_path" --keychain-profile "$VEKT_NOTARY_PROFILE" --wait
xcrun stapler staple "$package_path"
spctl --assess --type install --verbose=4 "$package_path"
