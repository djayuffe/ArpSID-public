# ArpSID macOS release signing/notarization proof

This RC now carries a reproducible release proof path instead of relying on ad-hoc signing or manual screenshots.

Required proof for a final macOS release:

1. Build with a Developer ID identity, not ad-hoc `-`.
2. Sign app/component/appex bundles with hardened runtime where applicable.
3. Verify bundle shape with `plutil`, `codesign --verify --deep --strict`, and `spctl --assess`.
4. Submit the packaged artifact using `xcrun notarytool submit --wait`.
5. Save the `notarytool log` output with the release artifact.
6. Staple and validate with `xcrun stapler staple` and `xcrun stapler validate`.
7. Run `auval -strict -v aumu ArpS ASID` after installation.

Command template:

```sh
scripts/macos/notarize_release.sh \
  "/path/with spaces/ArpSID Release(1).zip" \
  "developer@example.com" \
  "TEAMID1234" \
  "app-specific-password" \
  "com.arpsid.release"
```

All paths are quoted. Release packaging must work from directories with spaces and parentheses.
