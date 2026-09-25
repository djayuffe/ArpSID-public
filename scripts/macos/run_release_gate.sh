#!/usr/bin/env bash
set -euo pipefail

# ArpSID hard release gate. This script is intentionally fail-closed: if a
# shipping validator cannot be found, the gate fails instead of silently
# downgrading to a build-only check.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ARPSID_RELEASE_BUILD_DIR:-${TMPDIR:-/tmp}/arpsid-release-gate-build}"
VSTSDK_PATH_ARG="${VSTSDK_PATH:-${vst3sdk_SOURCE_DIR:-}}"
VST3_VALIDATOR="${VST3_VALIDATOR:-}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "ERROR: full ArpSID release gate must run on macOS/Darwin for AUv2/AUv3/Logic validation." >&2
  exit 2
fi

if [[ -z "$VSTSDK_PATH_ARG" ]]; then
  echo "ERROR: VSTSDK_PATH or vst3sdk_SOURCE_DIR must be set for RC parity builds." >&2
  exit 2
fi

if [[ -z "$VST3_VALIDATOR" ]]; then
  for cand in \
    "$VSTSDK_PATH_ARG/bin/validator" \
    "$VSTSDK_PATH_ARG/build/bin/validator" \
    "$VSTSDK_PATH_ARG/build/bin/Release/validator" \
    "$VSTSDK_PATH_ARG/build/bin/Debug/validator"; do
    if [[ -x "$cand" ]]; then VST3_VALIDATOR="$cand"; break; fi
  done
fi

if [[ -z "$VST3_VALIDATOR" || ! -x "$VST3_VALIDATOR" ]]; then
  echo "ERROR: VST3_VALIDATOR must point to Steinberg's validator executable." >&2
  exit 2
fi

if [[ "$BUILD_DIR" == "$ROOT" || "$BUILD_DIR" == "$ROOT"/* ]]; then
  echo "ERROR: ARPSID_RELEASE_BUILD_DIR must be outside the source tree; refusing to pollute release sources." >&2
  exit 2
fi

rm -rf "$BUILD_DIR"
cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DARPSID_BUILD_ALL=ON \
  -DARPSID_BUILD_TESTS=ON \
  -DARPSID_BUILD_AUV2_SMOKE=ON \
  -Dvst3sdk_SOURCE_DIR="$VSTSDK_PATH_ARG"

cmake --build "$BUILD_DIR" --config Release --parallel "$(sysctl -n hw.ncpu)"
ctest --test-dir "$BUILD_DIR" -C Release --output-on-failure

AUV2_COMPONENT="$BUILD_DIR/ArpSID.component"
if [[ ! -d "$AUV2_COMPONENT" ]]; then
  AUV2_COMPONENT="$(find "$BUILD_DIR" -maxdepth 8 -type d -name 'ArpSID.component' | head -n 1)"
fi
if [[ -z "$AUV2_COMPONENT" || ! -d "$AUV2_COMPONENT" ]]; then
  echo "ERROR: built AUv2 ArpSID.component not found." >&2
  exit 2
fi

/bin/sh "$ROOT/scripts/macos/verify_auv2_component.sh" --skip-auval "$AUV2_COMPONENT"
AUV2_INSTALL_DIR="$HOME/Library/Audio/Plug-Ins/Components"
AUV2_INSTALLED_COMPONENT="$AUV2_INSTALL_DIR/ArpSID.component"
/bin/sh "$ROOT/scripts/macos/install_auv2_component.sh" "$AUV2_COMPONENT" "$AUV2_INSTALL_DIR" "${ARPSID_CODESIGN_IDENTITY:--}"
/bin/sh "$ROOT/scripts/macos/refresh_auv2_component.sh" "$AUV2_INSTALLED_COMPONENT"
/bin/sh "$ROOT/scripts/macos/verify_auv2_component.sh" "$AUV2_INSTALLED_COMPONENT"

auval -strict -v aumu ArpS ASID
auval -strict -v aumu ArIn ASID
auval -strict -v aumu DrSD ASID
auval -strict -v aumu S808 ASID
auval -strict -v aumu C64P ASID
pluginkit -v -a "$AUV2_INSTALLED_COMPONENT"

VST3_BUNDLE="$(find "$BUILD_DIR" -name 'ArpSID.vst3' -type d -maxdepth 8 | head -n 1)"
if [[ -z "$VST3_BUNDLE" || ! -d "$VST3_BUNDLE" ]]; then
  echo "ERROR: built VST3 bundle not found." >&2
  exit 2
fi
"$VST3_VALIDATOR" "$VST3_BUNDLE"

echo "ArpSID RELEASE GATE PASS"
