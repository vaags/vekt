# macOS Release Gates

Build a signed VST3 and AUv3 with an explicit `VEKT_AUV3_APP_GROUP_ID` that is
provisioned for the release team. The identifier is intentionally not committed.

Run `validate-plugins.sh` with the VST3 and AUv3 bundle paths after installation.
It requires `pluginval`, `auval`, and valid signatures. Run `notarize.sh` only on
a signed installer package with `VEKT_SIGNING_IDENTITY` and `VEKT_NOTARY_PROFILE`
configured in the local keychain.

Before release, complete DAW smoke tests in Ableton Live, Logic Pro, and
GarageBand; verify AUv3 app-group preset storage; and capture worst-case 4x
oversampling measurements on Apple M4-class hardware.