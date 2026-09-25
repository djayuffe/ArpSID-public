#!/bin/sh
# refresh_auv2_component.sh — invalidate the Audio Component cache for an
# installed AUv2 .component bundle so Logic / GarageBand / auval pick it up.
#
# Usage:
#   refresh_auv2_component.sh [installed_component_path]
#
# macOS Audio Component cache notes (macOS 12+)
# ─────────────────────────────────────────────
# On macOS 12 Monterey and later the Audio Component cache is managed by
# AudioComponentRegistrar (a per-session LaunchAgent). The correct way to
# force re-discovery is:
#   1. Touch the bundle and its parent directory so the file-system mtime
#      change triggers the registrar's FSEvents watcher.
#   2. Use `pluginkit -v -a <path>` to explicitly register the bundle with
#      the PlugInKit / XPC Extension daemon (needed for AUv3 .appex; harmless
#      for .component bundles because the daemon ignores non-XPC bundles but
#      never errors out).
#   3. Wait briefly for the registrar to process the FSEvents notification.
#
# Do NOT use `killall -9 AudioComponentRegistrar`. On macOS 12+ the registrar
# is a LaunchAgent; killing it with -9 causes launchd to immediately restart
# it in a state where it has already processed the old bundle, so the cache
# refresh is a no-op. Worse, -9 skips the daemon's cache flush path, leaving
# stale entries in ~/Library/Caches/AudioUnitCache.
set -eu

DEFAULT_COMPONENT_PATH="$HOME/Library/Audio/Plug-Ins/Components/ArpSID.component"
COMPONENT_PATH="${1:-$DEFAULT_COMPONENT_PATH}"

if [ ! -d "$COMPONENT_PATH" ]; then
    echo "refresh_auv2_component: component not found: $COMPONENT_PATH" >&2
    exit 1
fi

COMPONENT_DIR="$(dirname "$COMPONENT_PATH")"

# ── Strip quarantine (belt-and-suspenders; install script also does this) ─────
/usr/bin/xattr -dr com.apple.quarantine "$COMPONENT_PATH" 2>/dev/null || true

# Fast path: a normal install only needs quarantine stripping + touches.
# Set ARPSID_AUV2_HARD_REFRESH=1 when recovering from a corrupted/stale AU cache.
if [ "${ARPSID_AUV2_HARD_REFRESH:-0}" != "1" ]; then
    /usr/bin/touch "$COMPONENT_PATH"
    /usr/bin/touch "$COMPONENT_PATH/Contents/Info.plist" 2>/dev/null || true
    /usr/bin/touch "$COMPONENT_DIR"
    if [ -x "/usr/bin/pluginkit" ]; then
        /usr/bin/pluginkit -v -a "$COMPONENT_PATH" 2>/dev/null || true
    fi
    echo "refresh_auv2_component: fast refresh triggered for $COMPONENT_PATH"
    exit 0
fi

# ── Hard local-dev cache eviction.  Logic/auval can keep a bad registry row
#    after a malformed .component was previously copied into the install path.
#    Removing only these per-user AU cache files is safe; they are rebuilt by
#    AudioComponentRegistrar on demand.
CACHE_DIR="$HOME/Library/Caches/AudioUnitCache"
/bin/rm -f "$CACHE_DIR/com.apple.audiounits.cache" 2>/dev/null || true
/bin/rm -f "$CACHE_DIR/com.apple.audiounits.sandboxed.cache" 2>/dev/null || true
/bin/rm -f "$CACHE_DIR/com.apple.audiounits.scan.cache" 2>/dev/null || true

# Stop stale per-user scanner/host helpers after cache eviction.  launchd will
# restart AudioComponentRegistrar cleanly when auval/Logic asks for components.
/usr/bin/killall AudioComponentRegistrar 2>/dev/null || true
/usr/bin/killall AUHostingService 2>/dev/null || true
/usr/bin/killall com.apple.audio.InfoHelper 2>/dev/null || true

# ── Touch bundle + parent to trigger FSEvents watcher ────────────────────────
/usr/bin/touch "$COMPONENT_PATH"
/usr/bin/touch "$COMPONENT_PATH/Contents/Info.plist" 2>/dev/null || true
/usr/bin/touch "$COMPONENT_DIR"

# ── Explicit PlugInKit registration (safe no-op for non-XPC bundles) ─────────
if [ -x "/usr/bin/pluginkit" ]; then
    /usr/bin/pluginkit -v -a "$COMPONENT_PATH" 2>/dev/null || true
fi

# ── Brief wait for AudioComponentRegistrar to process the FSEvents change ─────
sleep 1

echo "refresh_auv2_component: cache refresh triggered for $COMPONENT_PATH"
