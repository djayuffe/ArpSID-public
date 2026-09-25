#!/bin/sh
# sign_auv2_component.sh — codesign an ArpSID AUv2 .component bundle
#
# Usage:
#   sign_auv2_component.sh <component.bundle> <identity> <entitlements> [hardened_runtime=ON|OFF]
#
# <component.bundle>  — path to the built ArpSID.component directory
# <identity>          — codesign identity: "-" for ad-hoc, or a Developer ID
# <entitlements>      — path to the .entitlements plist (must NOT have app-sandbox)
# hardened_runtime    — pass ON to add --options runtime (for notarisation)
#
# AUv2 signing notes
# ──────────────────
# 1. Sign the Mach-O binary FIRST, then the bundle — "inside-out" ordering.
# 2. For .component bundles that ship no nested frameworks/dylibs, a single
#    codesign invocation with --deep is sufficient.
# 3. Do NOT pass --timestamp=none for Developer ID notarisation builds (omit
#    the flag entirely — codesign defaults to Apple's timestamp server).
# 4. The entitlements file must NOT contain com.apple.security.app-sandbox.
#    Passing a sandboxed entitlements file to codesign for an in-process plugin
#    propagates the sandbox to the host and causes auval to fail.
# 5. Ad-hoc signing ("-") is sufficient for local development and auval.

set -eu

COMPONENT_BUNDLE="${1:?component bundle path missing}"
IDENTITY="${2:--}"
ENTITLEMENTS="${3:-}"
HARDENED_RUNTIME="${4:-OFF}"

CODESIGN_BIN="/usr/bin/codesign"

if [ ! -d "$COMPONENT_BUNDLE" ]; then
    echo "sign_auv2_component: bundle not found: $COMPONENT_BUNDLE" >&2
    exit 1
fi

if [ ! -x "$CODESIGN_BIN" ]; then
    echo "sign_auv2_component: codesign not found at $CODESIGN_BIN" >&2
    exit 1
fi

# Build codesign argument list
ARGS="--force --sign \"$IDENTITY\" --timestamp=none"
if [ "$HARDENED_RUNTIME" = "ON" ]; then
    ARGS="$ARGS --options runtime"
fi
if [ -n "$ENTITLEMENTS" ] && [ -f "$ENTITLEMENTS" ]; then
    ARGS="$ARGS --entitlements \"$ENTITLEMENTS\""
fi
ARGS="$ARGS --deep \"$COMPONENT_BUNDLE\""

# shellcheck disable=SC2086
eval "$CODESIGN_BIN $ARGS"

# Verify the signature
"$CODESIGN_BIN" --verify --deep --strict --verbose=2 "$COMPONENT_BUNDLE"

echo "sign_auv2_component: signed OK: $COMPONENT_BUNDLE"
