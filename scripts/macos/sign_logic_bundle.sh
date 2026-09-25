#!/bin/sh
set -eu

APP_PATH="${1:?app path missing}"
IDENTITY="${2:--}"
HOST_ENTITLEMENTS="${3:?host entitlements missing}"
EXT_ENTITLEMENTS="${4:?extension entitlements missing}"
HARDENED_RUNTIME="${5:-OFF}"

if [ ! -d "$APP_PATH" ]; then
  echo "App bundle missing: $APP_PATH" >&2
  exit 1
fi

APPEX="$APP_PATH/Contents/PlugIns/arpsid_auv3.appex"
APP_EXECUTABLE=$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" \
  "$APP_PATH/Contents/Info.plist")
APP_BIN="$APP_PATH/Contents/MacOS/$APP_EXECUTABLE"
APPEX_BIN="$APPEX/Contents/MacOS/arpsid_auv3"
if [ ! -d "$APPEX" ]; then
  echo "Embedded AUv3 appex missing: $APPEX" >&2
  exit 1
fi

RUNTIME_ARGS=""
if [ "$HARDENED_RUNTIME" = "ON" ]; then
  RUNTIME_ARGS="--options runtime"
fi

/usr/bin/xattr -dr com.apple.quarantine "$APP_PATH" || true

if [ -f "$APPEX_BIN" ]; then
  /usr/bin/codesign --force --sign "$IDENTITY" --timestamp=none $RUNTIME_ARGS \
    --entitlements "$EXT_ENTITLEMENTS" "$APPEX_BIN"
fi
/usr/bin/codesign --force --sign "$IDENTITY" --timestamp=none $RUNTIME_ARGS \
  --entitlements "$EXT_ENTITLEMENTS" "$APPEX"
if [ -f "$APP_BIN" ]; then
  /usr/bin/codesign --force --sign "$IDENTITY" --timestamp=none $RUNTIME_ARGS "$APP_BIN"
fi
/usr/bin/codesign --force --sign "$IDENTITY" --timestamp=none $RUNTIME_ARGS \
  --entitlements "$HOST_ENTITLEMENTS" "$APP_PATH"
/usr/bin/codesign --verify --deep --strict --verbose=2 "$APP_PATH"
