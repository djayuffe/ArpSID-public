#!/bin/sh
# verify_auv2_component.sh — structurally validate an ArpSID AUv2
# .component bundle and optionally run auval for registered installs.
#
# Usage:
#   verify_auv2_component.sh [--skip-auval] [component_path]
#
# Exit codes:
#   0 — all checks passed (or auval not available and structural checks passed)
#   1 — one or more checks failed
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
DEFAULT_COMPONENT_PATH="$HOME/Library/Audio/Plug-Ins/Components/ArpSID.component"
RUN_AUVAL=1
COMPONENT_PATH="$DEFAULT_COMPONENT_PATH"
AUV2_AUVAL_RETRIES="${ARPSID_AUVAL_RETRIES:-2}"
AUV2_AUVAL_RETRY_DELAY="${ARPSID_AUVAL_RETRY_DELAY:-1}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --skip-auval)
            RUN_AUVAL=0
            ;;
        --help|-h)
            echo "Usage: verify_auv2_component.sh [--skip-auval] [component_path]"
            exit 0
            ;;
        *)
            COMPONENT_PATH="$1"
            ;;
    esac
    shift
done

PLIST="$COMPONENT_PATH/Contents/Info.plist"
BIN="$COMPONENT_PATH/Contents/MacOS/ArpSID"
FAIL=0

fail() {
    echo "FAIL: $1" >&2
    FAIL=1
}

# ── Bundle exists ─────────────────────────────────────────────────────────────
if [ ! -d "$COMPONENT_PATH" ]; then
    echo "verify_auv2_component: bundle not found: $COMPONENT_PATH" >&2
    exit 1
fi

# ── Required files ────────────────────────────────────────────────────────────
[ -f "$PLIST" ] || fail "Info.plist missing: $PLIST"
[ -f "$BIN"   ] || fail "Executable missing: $BIN"

if [ -f "$BIN" ]; then
    if /usr/bin/nm -gU "$BIN" 2>/dev/null | grep -q '_ArpSIDAUv2Factory'; then
        echo "OK: Mach-O exports _ArpSIDAUv2Factory"
    else
        fail "Mach-O does not export _ArpSIDAUv2Factory"
    fi
fi

# ── plutil lint — catches malformed XML including stray comments ──────────────
if /usr/bin/plutil -lint "$PLIST" >/dev/null 2>&1; then
    echo "OK: plutil -lint passed"
else
    fail "plutil -lint failed: $PLIST — plist is malformed (check for XML comments, bad encoding, or literal \${VAR} tokens)"
fi

# ── AudioComponents entries required keys ────────────────────────────────────
check_plist_key() {
    local keypath="$1" expected="$2"
    local actual
    actual=$(/usr/bin/plutil -extract "$keypath" raw -expect string "$PLIST" 2>/dev/null || echo "__MISSING__")
    if [ "$actual" = "$expected" ]; then
        echo "OK: $keypath = $actual"
    else
        fail "$keypath expected '$expected', got '$actual'"
    fi
}

check_component_entry() {
    local idx="$1" subtype="$2" expected_name="$3"
    check_plist_key "AudioComponents.$idx.type"            "aumu"
    check_plist_key "AudioComponents.$idx.subtype"         "$subtype"
    check_plist_key "AudioComponents.$idx.manufacturer"    "ASID"
    check_plist_key "AudioComponents.$idx.factoryFunction" "ArpSIDAUv2Factory"
    check_plist_key "AudioComponents.$idx.NSViewFactory"   "ArpSIDAUv2ViewFactory"
    check_plist_key "AudioComponents.$idx.name"            "$expected_name"

    local version_int
    version_int=$(/usr/bin/plutil -extract "AudioComponents.$idx.version" raw -expect integer "$PLIST" 2>/dev/null || echo "0")
    if echo "$version_int" | grep -qE '^[1-9][0-9]+$'; then
        echo "OK: AudioComponents.$idx.version = $version_int"
    else
        fail "AudioComponents.$idx.version is '$version_int' — expected a real integer (CMake variable substitution may have failed)"
    fi
}

check_component_entry 0 "ArpS" "Uber Sound Solutions: ArpSID"
check_component_entry 1 "ArIn" "Uber Sound Solutions: Pure Instrument"
check_component_entry 2 "DrSD" "Uber Sound Solutions: DrSID Drum Machine"
check_component_entry 3 "S808" "Uber Sound Solutions: SID-808"
check_component_entry 4 "C64P" "Uber Sound Solutions: C64 SID Player"
check_plist_key "CFBundleIconFile" "ArpSIDHybrid.png"

# ── CFBundleIdentifier must not contain literal '${' ─────────────────────────
BUNDLE_ID=$(/usr/bin/plutil -extract "CFBundleIdentifier" raw -expect string "$PLIST" 2>/dev/null || echo "")
if echo "$BUNDLE_ID" | grep -q '\${'; then
    fail "CFBundleIdentifier contains literal '\${' — CMake variable substitution failed"
else
echo "OK: CFBundleIdentifier = $BUNDLE_ID"
fi

check_resource_file() {
    local path="$1"
    if [ -f "$path" ]; then
        echo "OK: resource present: $(basename "$path")"
    else
        fail "missing AUv2 icon resource: $path"
    fi
}

check_resource_file "$COMPONENT_PATH/Contents/Resources/ArpSIDHybrid.png"
check_resource_file "$COMPONENT_PATH/Contents/Resources/ArpSIDInstrument.png"
check_resource_file "$COMPONENT_PATH/Contents/Resources/ArpSIDDrumMachine.png"
check_resource_file "$COMPONENT_PATH/Contents/Resources/ArpSIDSid808.png"

# ── Codesign verification ─────────────────────────────────────────────────────
if /usr/bin/codesign --verify --deep --strict --verbose=2 "$COMPONENT_PATH" 2>/dev/null; then
    echo "OK: codesign --verify passed"
else
    # Warn but don't fail: ad-hoc signed or unsigned builds still work locally
    echo "WARN: codesign --verify failed (ad-hoc or unsigned — acceptable for local dev)"
fi

# ── auval targeted validation ─────────────────────────────────────────────────
if [ "$RUN_AUVAL" -eq 0 ]; then
    if [ "$FAIL" -eq 0 ]; then
        echo "verify_auv2_component: structural checks passed (auval skipped): $COMPONENT_PATH"
    else
        echo "verify_auv2_component: FAILED structural checks: $COMPONENT_PATH" >&2
    fi
    exit "$FAIL"
fi

AUVAL_BIN=""
for candidate in /usr/bin/auval /usr/local/bin/auval /usr/bin/auvaltool; do
    if [ -x "$candidate" ]; then
        AUVAL_BIN="$candidate"
        break
    fi
done

if [ -z "$AUVAL_BIN" ] && command -v auval >/dev/null 2>&1; then
    AUVAL_BIN="$(command -v auval)"
fi

if [ -z "$AUVAL_BIN" ]; then
    echo "WARN: auval/auvaltool not installed — skipping active AU validation"
    if [ "$FAIL" -eq 0 ]; then
        echo "verify_auv2_component: structural checks passed (no auval available): $COMPONENT_PATH"
    else
        echo "verify_auv2_component: FAILED structural checks: $COMPONENT_PATH" >&2
    fi
    exit "$FAIL"
fi

canonical_path() {
    local path="$1"
    local base dir
    dir=$(dirname "$path")
    base=$(basename "$path")
    if [ -d "$dir" ]; then
        (cd "$dir" 2>/dev/null && printf '%s/%s\n' "$(pwd -P)" "$base") || printf '%s\n' "$path"
    else
        printf '%s\n' "$path"
    fi
}

FOUND_COMPONENTS="$(find "$HOME/Library/Audio/Plug-Ins/Components" "/Library/Audio/Plug-Ins/Components" \
    -maxdepth 1 -name "ArpSID.component" -type d 2>/dev/null || true)"
FOUND_COUNT="$(printf '%s\n' "$FOUND_COMPONENTS" | sed '/^$/d' | wc -l | tr -d ' ')"
if [ "$FOUND_COUNT" != "1" ]; then
    fail "expected exactly one installed ArpSID.component before auval, found $FOUND_COUNT"
    printf '%s\n' "$FOUND_COMPONENTS" >&2
else
    FOUND_ONE="$(printf '%s\n' "$FOUND_COMPONENTS" | sed '/^$/d' | head -n 1)"
    if [ "$(canonical_path "$FOUND_ONE")" != "$(canonical_path "$COMPONENT_PATH")" ]; then
        fail "auval registry component path differs from verified path: found $FOUND_ONE, verifying $COMPONENT_PATH"
    else
        echo "OK: exactly one installed ArpSID.component: $FOUND_ONE"
    fi
fi

VAL_LOG="$(mktemp)"
cleanup() { rm -f "$VAL_LOG"; }
trap cleanup EXIT

run_auval() {
    local subtype="$1"
    local attempt=1
    while [ "$attempt" -le "$AUV2_AUVAL_RETRIES" ]; do
        : >"$VAL_LOG"
        echo "Running: $AUVAL_BIN -strict -v aumu $subtype ASID (attempt $attempt/$AUV2_AUVAL_RETRIES)"
        if "$AUVAL_BIN" -strict -v aumu "$subtype" ASID >"$VAL_LOG" 2>&1; then
            echo "OK: auval passed for subtype $subtype"
            cat "$VAL_LOG" >&2
            return 0
        fi

        if grep -q "didn't find the component\|Cannot get Component's Name strings\|Error from retrieving Component Version: -50" "$VAL_LOG" \
            && [ "$attempt" -lt "$AUV2_AUVAL_RETRIES" ]; then
            echo "WARN: AU registry not ready for subtype $subtype yet; retrying in ${AUV2_AUVAL_RETRY_DELAY}s" >&2
            if [ -f "$SCRIPT_DIR/refresh_auv2_component.sh" ]; then
                /bin/sh "$SCRIPT_DIR/refresh_auv2_component.sh" "$COMPONENT_PATH" >/dev/null 2>&1 || true
            fi
            sleep "$AUV2_AUVAL_RETRY_DELAY"
            attempt=$((attempt + 1))
            continue
        fi

        fail "auval -strict -v aumu $subtype ASID failed"
        cat "$VAL_LOG" >&2
        return 1
    done
}

SUBTYPES="${ARPSID_AUVAL_SUBTYPES:-ArpS ArIn DrSD S808 C64P}"
for subtype in $SUBTYPES; do
    run_auval "$subtype"
done

if [ "$FAIL" -eq 0 ]; then
    echo "verify_auv2_component: all checks passed: $COMPONENT_PATH"
else
    echo "verify_auv2_component: FAILED — see errors above: $COMPONENT_PATH" >&2
fi
exit "$FAIL"
