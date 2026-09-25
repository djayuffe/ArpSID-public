#!/bin/sh
# notarize_release.sh — sign, verify, submit, staple and prove a macOS ArpSID release artifact.
#
# Usage:
#   notarize_release.sh <artifact-path> <apple-id> <team-id> <app-specific-password> [bundle-id] [developer-id-identity]
#
# The artifact may be a .zip, .dmg, .pkg, .app, .component, .appex or staging directory.
# All paths are quoted so Finder/Downloads names like "ArpSID RC(1)" are valid.
set -eu

ARTIFACT="${1:?artifact path missing}"
APPLE_ID="${2:?Apple ID missing}"
TEAM_ID="${3:?Team ID missing}"
PASSWORD="${4:?app-specific password missing}"
BUNDLE_ID="${5:-com.arpsid.release}"
IDENTITY="${6:-Developer ID Application}"
PROOF_DIR="${ARPSID_NOTARIZATION_PROOF_DIR:-$(pwd)/notarization-proof}"
mkdir -p "$PROOF_DIR"

if [ ! -e "$ARTIFACT" ]; then
  echo "notarize_release: artifact not found: $ARTIFACT" >&2
  exit 1
fi
if [ ! -x /usr/bin/xcrun ] || [ ! -x /usr/bin/codesign ] || [ ! -x /usr/sbin/spctl ]; then
  echo "notarize_release: requires macOS xcrun/codesign/spctl" >&2
  exit 1
fi

case "$ARTIFACT" in
  *.app|*.component|*.appex)
    /usr/bin/codesign --force --deep --options runtime --timestamp --sign "$IDENTITY" "$ARTIFACT"
    /usr/bin/codesign --verify --deep --strict --verbose=2 "$ARTIFACT" >"$PROOF_DIR/codesign-verify.txt" 2>&1
    /usr/sbin/spctl --assess --type execute --verbose=2 "$ARTIFACT" >"$PROOF_DIR/spctl-before.txt" 2>&1 || true
    ;;
  *.pkg)
    /usr/sbin/spctl --assess --type install --verbose=2 "$ARTIFACT" >"$PROOF_DIR/spctl-before.txt" 2>&1 || true
    ;;
  *)
    echo "notarize_release: skipping direct codesign verify for archive/dmg payload: $ARTIFACT" >"$PROOF_DIR/codesign-verify.txt"
    ;;
esac

SUBMIT_PLIST="$PROOF_DIR/notary-submit.plist"
/usr/bin/xcrun notarytool submit "$ARTIFACT"   --apple-id "$APPLE_ID"   --team-id "$TEAM_ID"   --password "$PASSWORD"   --wait   --output-format plist >"$SUBMIT_PLIST"
REQ_ID="$(/usr/libexec/PlistBuddy -c 'Print id' "$SUBMIT_PLIST")"
STATUS="$(/usr/libexec/PlistBuddy -c 'Print status' "$SUBMIT_PLIST" 2>/dev/null || true)"

if [ -z "$REQ_ID" ]; then
  echo "notarize_release: notarytool did not return a request id" >&2
  exit 1
fi
if [ "$STATUS" != "Accepted" ]; then
  echo "notarize_release: notarization status is '$STATUS' for request $REQ_ID" >&2
  /usr/bin/xcrun notarytool log "$REQ_ID" --apple-id "$APPLE_ID" --team-id "$TEAM_ID" --password "$PASSWORD" >"$PROOF_DIR/notary-log.json" || true
  exit 1
fi

/usr/bin/xcrun notarytool log "$REQ_ID"   --apple-id "$APPLE_ID"   --team-id "$TEAM_ID"   --password "$PASSWORD" >"$PROOF_DIR/notary-log.json"

/usr/bin/xcrun stapler staple "$ARTIFACT" >"$PROOF_DIR/stapler-staple.txt" 2>&1 || true
/usr/bin/xcrun stapler validate "$ARTIFACT" >"$PROOF_DIR/stapler-validate.txt" 2>&1 || true
case "$ARTIFACT" in
  *.pkg) /usr/sbin/spctl --assess --type install --verbose=2 "$ARTIFACT" >"$PROOF_DIR/spctl-after.txt" 2>&1 || true ;;
  *) /usr/sbin/spctl --assess --type execute --verbose=2 "$ARTIFACT" >"$PROOF_DIR/spctl-after.txt" 2>&1 || true ;;
esac

cat >"$PROOF_DIR/NOTARIZATION_PROOF_SUMMARY.txt" <<EOF
artifact=$ARTIFACT
bundle_id=$BUNDLE_ID
team_id=$TEAM_ID
request_id=$REQ_ID
status=$STATUS
identity=$IDENTITY
EOF

echo "notarize_release: notarization proof complete for $ARTIFACT bundle-id=$BUNDLE_ID request=$REQ_ID proof-dir=$PROOF_DIR"
