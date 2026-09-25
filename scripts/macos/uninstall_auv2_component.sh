#!/bin/sh
# uninstall_auv2_component.sh — remove installed ArpSID AUv2 .component
#
# Usage:
#   uninstall_auv2_component.sh [--user | --system | --all]
#
# --user    Remove from ~/Library/Audio/Plug-Ins/Components   (default)
# --system  Remove from /Library/Audio/Plug-Ins/Components    (requires sudo)
# --all     Remove from both locations
#
# After removal the Audio Component cache is invalidated so DAWs stop
# discovering the component immediately.

set -eu

MODE="${1:---user}"

USER_DIR="$HOME/Library/Audio/Plug-Ins/Components/ArpSID.component"
SYS_DIR="/Library/Audio/Plug-Ins/Components/ArpSID.component"

remove_one() {
    local path="$1"
    if [ -d "$path" ]; then
        rm -rf "$path"
        # Invalidate the parent directory mtime so AudioComponentRegistrar
        # removes the stale entry from its cache.
        touch "$(dirname "$path")" 2>/dev/null || true
        echo "Removed: $path"
    else
        echo "Not installed: $path"
    fi
}

case "$MODE" in
    --user)
        remove_one "$USER_DIR" ;;
    --system)
        if [ "$(id -u)" -ne 0 ]; then
            echo "error: system uninstall requires sudo" >&2
            exit 1
        fi
        remove_one "$SYS_DIR" ;;
    --all)
        remove_one "$USER_DIR"
        if [ -d "$SYS_DIR" ]; then
            if [ "$(id -u)" -ne 0 ]; then
                echo "warning: system component at $SYS_DIR requires sudo to remove" >&2
            else
                remove_one "$SYS_DIR"
            fi
        fi ;;
    *)
        echo "Usage: $0 [--user | --system | --all]" >&2
        exit 1 ;;
esac

# Trigger AudioComponentRegistrar cache invalidation (macOS 12+).
# pluginkit -v -a on a non-existent path is a no-op; touch on the parent
# directory is the actual invalidation trigger.
if [ -x /usr/bin/pluginkit ]; then
    /usr/bin/pluginkit -v -a "$USER_DIR" 2>/dev/null || true
fi

echo "Cache invalidation triggered — restart any open DAW to complete uninstall."
