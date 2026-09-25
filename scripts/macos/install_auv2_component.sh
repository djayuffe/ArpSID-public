#!/bin/sh
# install_auv2_component.sh — install ArpSID AUv2 .component bundle
#
# Usage:
#   install_auv2_component.sh <built_bundle_or_build_dir> [install_dir] [identity]
#
# Examples:
#   scripts/macos/install_auv2_component.sh build-auv2 "$HOME/Library/Audio/Plug-Ins/Components" -
#   scripts/macos/install_auv2_component.sh build-auv2/ArpSID.component "$HOME/Library/Audio/Plug-Ins/Components" -
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
DEFAULT_INSTALL_DIR="$HOME/Library/Audio/Plug-Ins/Components"

SRC_ARG="${1:?built component bundle or build directory missing}"
INSTALL_DIR="${2:-$DEFAULT_INSTALL_DIR}"
IDENTITY="${3:--}"
ENTITLEMENTS="${4:-$SCRIPT_DIR/../../packaging/macos/ArpSIDAUv2.entitlements}"
COMPONENT_DST="$INSTALL_DIR/ArpSID.component"

resolve_component() {
    src="$1"
    if [ -d "$src/Contents" ] && [ -f "$src/Contents/Info.plist" ]; then
        printf '%s\n' "$src"
        return 0
    fi
    if [ -d "$src/ArpSID.component/Contents" ] && [ -f "$src/ArpSID.component/Contents/Info.plist" ]; then
        printf '%s\n' "$src/ArpSID.component"
        return 0
    fi
    found="$(find "$src" -maxdepth 6 -type d -name 'ArpSID.component' -print -quit 2>/dev/null || true)"
    if [ -n "$found" ] && [ -f "$found/Contents/Info.plist" ]; then
        printf '%s\n' "$found"
        return 0
    fi
    return 1
}

if [ ! -d "$SRC_ARG" ]; then
    echo "install_auv2_component: source path not found: $SRC_ARG" >&2
    exit 1
fi

COMPONENT_SRC="$(resolve_component "$SRC_ARG" || true)"
if [ -z "$COMPONENT_SRC" ]; then
    echo "install_auv2_component: no valid ArpSID.component found under: $SRC_ARG" >&2
    exit 1
fi
if [ ! -f "$COMPONENT_SRC/Contents/Info.plist" ]; then
    echo "install_auv2_component: invalid component bundle: $COMPONENT_SRC" >&2
    exit 1
fi

/bin/mkdir -p "$INSTALL_DIR"
if [ -d "$COMPONENT_DST" ]; then
    /bin/rm -rf "$COMPONENT_DST"
fi
/usr/bin/ditto "$COMPONENT_SRC" "$COMPONENT_DST"
/usr/bin/xattr -dr com.apple.quarantine "$COMPONENT_DST" 2>/dev/null || true

# Validate bundle shape before signing so mistakes fail early.
if [ ! -f "$COMPONENT_DST/Contents/Info.plist" ] || [ ! -f "$COMPONENT_DST/Contents/MacOS/ArpSID" ]; then
    echo "install_auv2_component: installed bundle format invalid: $COMPONENT_DST" >&2
    exit 1
fi
/usr/bin/plutil -lint "$COMPONENT_DST/Contents/Info.plist" >/dev/null
for idx in 0 1 2 3 4; do
    /usr/bin/plutil -extract "AudioComponents.$idx.name" raw -expect string "$COMPONENT_DST/Contents/Info.plist" >/dev/null
    /usr/bin/plutil -extract "AudioComponents.$idx.version" raw -expect integer "$COMPONENT_DST/Contents/Info.plist" >/dev/null
done

if [ -n "$IDENTITY" ] && [ -x "/usr/bin/codesign" ]; then
    if [ -f "$ENTITLEMENTS" ]; then
        /usr/bin/codesign --force --sign "$IDENTITY" --timestamp=none --entitlements "$ENTITLEMENTS" --deep "$COMPONENT_DST"
    else
        /usr/bin/codesign --force --sign "$IDENTITY" --timestamp=none --deep "$COMPONENT_DST"
    fi
fi

if [ -f "$SCRIPT_DIR/refresh_auv2_component.sh" ]; then
    /bin/sh "$SCRIPT_DIR/refresh_auv2_component.sh" "$COMPONENT_DST"
fi

echo "install_auv2_component: installed $COMPONENT_SRC to $COMPONENT_DST"
