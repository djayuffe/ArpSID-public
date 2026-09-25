#!/bin/sh
set -eu

APP_PATH="${1:?installed app missing}"
EXT_BUNDLE_ID="${2:?extension bundle id missing}"
APPEX="$APP_PATH/Contents/PlugIns/arpsid_auv3.appex"
APP_INFO="$APP_PATH/Contents/Info.plist"

if [ ! -d "$APP_PATH" ]; then
  echo "Installed app missing: $APP_PATH" >&2
  exit 1
fi
if [ ! -d "$APPEX" ]; then
  echo "Embedded appex missing: $APPEX" >&2
  exit 1
fi
if [ ! -f "$APP_INFO" ]; then
  echo "Installed app plist missing: $APP_INFO" >&2
  exit 1
fi

APP_BUNDLE_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP_INFO" 2>/dev/null || true)"
APP_EXECUTABLE="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP_INFO" 2>/dev/null || true)"

stop_container_app() {
  if [ -n "$APP_BUNDLE_ID" ]; then
    /usr/bin/osascript -e "tell application id \"$APP_BUNDLE_ID\" to quit" >/dev/null 2>&1 || true
  fi
  sleep 1
  if [ -n "$APP_EXECUTABLE" ]; then
    /usr/bin/killall -9 "$APP_EXECUTABLE" >/dev/null 2>&1 || true
  fi
  # The current AUv3 container executable is "ArpSID AUv3" even when the
  # bundle is installed as ArpSID.app for Logic discovery.
  /usr/bin/killall -9 "ArpSID AUv3" >/dev/null 2>&1 || true
  /usr/bin/killall -9 "ArpSID" >/dev/null 2>&1 || true
}

if [ -w "$APP_PATH" ] || [ -w "$(dirname "$APP_PATH")" ]; then
  /usr/bin/xattr -dr com.apple.quarantine "$APP_PATH" >/dev/null 2>&1 || true
  /usr/bin/xattr -dr com.apple.quarantine "$APPEX" >/dev/null 2>&1 || true
else
  echo "note: $APP_PATH is not writable by the current user; skipping xattr cleanup. Rerun refresh with sudo if you want quarantine cleanup under /Applications." >&2
fi
stop_container_app
/usr/bin/pluginkit -r "$EXT_BUNDLE_ID" >/dev/null 2>&1 || true
/usr/bin/pluginkit -r "$APP_PATH" >/dev/null 2>&1 || true
/usr/bin/pluginkit -r "$APPEX" >/dev/null 2>&1 || true
/usr/bin/pluginkit -a "$APP_PATH"
/usr/bin/pluginkit -a "$APPEX"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APP_PATH" >/dev/null 2>&1 || true
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APPEX" >/dev/null 2>&1 || true
/usr/bin/killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
sleep 2

# Some macOS setups do not surface freshly installed app extensions to PluginKit
# until the container app has been launched at least once.
/usr/bin/open -gja "$APP_PATH" >/dev/null 2>&1 || true
sleep 2
stop_container_app
/usr/bin/killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
sleep 2

if ! /usr/bin/pluginkit -m -A -D | /usr/bin/grep -F "$EXT_BUNDLE_ID" >/dev/null 2>&1; then
  echo "pluginkit refresh completed, but the extension is still not registered: $EXT_BUNDLE_ID" >&2
  /usr/bin/codesign -dv --verbose=4 "$APP_PATH" >&2 || true
  /usr/bin/codesign -dv --verbose=4 "$APPEX" >&2 || true
  echo "Installed extension plist:" >&2
  /usr/bin/plutil -p "$APPEX/Contents/Info.plist" >&2 || true
  echo "pluginkit dump follows:" >&2
  /usr/bin/pluginkit -m -A -D >&2 || true
  stop_container_app
  exit 1
fi

echo "Refreshed pluginkit, LaunchServices, and AudioComponentRegistrar for: $APP_PATH"
