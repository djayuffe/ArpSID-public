#!/bin/sh
set -eu

APP_PATH="${1:?installed app missing}"
EXT_BUNDLE_ID="${2:?extension bundle id missing}"
APP_BUNDLE_ID="${3:?app bundle id missing}"
RECEIPT_PATH="${4:-}"
APPEX="$APP_PATH/Contents/PlugIns/arpsid_auv3.appex"
APP_PLIST="$APP_PATH/Contents/Info.plist"
EXT_PLIST="$APPEX/Contents/Info.plist"

if [ ! -d "$APP_PATH" ]; then
  echo "Installed app missing: $APP_PATH" >&2
  exit 1
fi
if [ ! -d "$APPEX" ]; then
  echo "Embedded appex missing: $APPEX" >&2
  exit 1
fi

/usr/bin/plutil -extract CFBundleIdentifier raw -expect string "$APP_PLIST" | /usr/bin/grep -qx "$APP_BUNDLE_ID"
/usr/bin/plutil -extract CFBundleIdentifier raw -expect string "$EXT_PLIST" | /usr/bin/grep -qx "$EXT_BUNDLE_ID"
APP_ID=$(/usr/bin/plutil -extract CFBundleIdentifier raw -expect string "$APP_PLIST")
EXT_ID=$(/usr/bin/plutil -extract CFBundleIdentifier raw -expect string "$EXT_PLIST")
case "$EXT_ID" in
  "$APP_ID".*) ;;
  *)
    echo "extension bundle id is not prefixed by app bundle id: $EXT_ID vs $APP_ID" >&2
    exit 1
    ;;
esac
/usr/bin/plutil -extract NSExtension.NSExtensionPointIdentifier raw -expect string "$EXT_PLIST" | /usr/bin/grep -qx 'com.apple.AudioUnit'
/usr/bin/plutil -extract NSExtension.NSExtensionPrincipalClass raw -expect string "$EXT_PLIST" | /usr/bin/grep -qx 'ArpSIDAUAudioUnitFactory'
if /usr/bin/plutil -extract NSExtension.NSExtensionMainStoryboard raw "$EXT_PLIST" >/dev/null 2>&1; then
  echo "AUv3 appex must not use NSExtensionMainStoryboard" >&2
  exit 1
fi
/usr/bin/plutil -extract NSExtension.NSExtensionAttributes.AudioComponents.0.type raw -expect string "$EXT_PLIST" | /usr/bin/grep -qx 'aumu'
/usr/bin/plutil -extract NSExtension.NSExtensionAttributes.AudioComponents.0.subtype raw -expect string "$EXT_PLIST" | /usr/bin/grep -qx 'ArpS'
/usr/bin/plutil -extract NSExtension.NSExtensionAttributes.AudioComponents.0.manufacturer raw -expect string "$EXT_PLIST" | /usr/bin/grep -qx 'ASID'
/usr/bin/codesign --verify --deep --strict --verbose=2 "$APP_PATH"
/usr/bin/codesign -dv --verbose=4 "$APP_PATH" >/dev/null 2>&1 || true
/usr/bin/codesign -dv --verbose=4 "$APPEX" >/dev/null 2>&1 || true

PLUGINKIT_LOG="$(mktemp)"
AUVAL_LIST_LOG="$(mktemp)"
AUVAL_VALIDATE_LOG="$(mktemp)"
cleanup() {
  rm -f "$PLUGINKIT_LOG" "$AUVAL_LIST_LOG" "$AUVAL_VALIDATE_LOG"
}
trap cleanup EXIT

/usr/bin/pluginkit -m -A -D >"$PLUGINKIT_LOG"
if ! /usr/bin/grep -F "$EXT_BUNDLE_ID" "$PLUGINKIT_LOG" >/dev/null 2>&1; then
  echo "pluginkit does not list the ArpSID AUv3 extension: $EXT_BUNDLE_ID" >&2
  echo "If this build is ad-hoc signed (-), rerun with a real Apple Development identity; current macOS may refuse to register ad-hoc app extensions." >&2
  echo "pluginkit output follows:" >&2
  cat "$PLUGINKIT_LOG" >&2
  exit 1
fi

AUVAL_BIN=""
if command -v auval >/dev/null 2>&1; then
  AUVAL_BIN="$(command -v auval)"
elif command -v auvaltool >/dev/null 2>&1; then
  AUVAL_BIN="$(command -v auvaltool)"
fi

if [ -n "$AUVAL_BIN" ]; then
  # Host-wide auval enumeration is advisory only. Third-party plug-ins on the machine may crash
  # global scanning even when ArpSID itself is correctly installed.
  if "$AUVAL_BIN" -a >"$AUVAL_LIST_LOG" 2>&1; then
    echo "auval -a output:" >&2
    cat "$AUVAL_LIST_LOG" >&2
  else
    echo "warning: auval -a failed on this machine; ignoring global enumeration and using targeted validation only" >&2
    cat "$AUVAL_LIST_LOG" >&2
  fi

  if ! "$AUVAL_BIN" -v aumu ArpS ASID >"$AUVAL_VALIDATE_LOG" 2>&1; then
    echo "targeted auval validation failed for aumu/ArpS/ASID" >&2
    cat "$AUVAL_VALIDATE_LOG" >&2
    exit 1
  fi

  cat "$AUVAL_VALIDATE_LOG" >&2
else
  echo "neither auval nor auvaltool is installed; active AUv3 validation cannot be asserted" >&2
  exit 1
fi

if [ -n "$RECEIPT_PATH" ]; then
  RECEIPT_DIR=$(/usr/bin/dirname "$RECEIPT_PATH")
  /bin/mkdir -p "$RECEIPT_DIR"
  {
    echo "ARPSID_AUV3_REGISTRATION_VERIFIED=1"
    echo "APP_BUNDLE_ID=$APP_BUNDLE_ID"
    echo "EXT_BUNDLE_ID=$EXT_BUNDLE_ID"
    echo "APP_PATH=$APP_PATH"
    echo "VERIFIED_UTC=$(/bin/date -u '+%Y-%m-%dT%H:%M:%SZ')"
  } >"$RECEIPT_PATH"
fi

echo "Logic AUv3 install verified: $APP_PATH"
