#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD:-$ROOT/build-release-macos}"
PROOF="${PROOF:-$ROOT/release-proof/macos-gate-$(date +%Y%m%d-%H%M%S)}"
AU_TYPE="${AU_TYPE:-aumu}"
AU_SUBTYPE="${AU_SUBTYPE:-ArpS}"
AU_MANUFACTURER="${AU_MANUFACTURER:-ASID}"

mkdir -p "$PROOF"

{
  echo "ROOT=$ROOT"
  echo "BUILD=$BUILD"
  echo "PROOF=$PROOF"
  echo "DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  sw_vers || true
  uname -a
  xcodebuild -version || true
} | tee "$PROOF/environment.txt"

cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DARPSID_BUILD_TESTS=ON \
  -DARPSID_BUILD_AUV2=ON \
  -DARPSID_BUILD_AUV3=ON \
  -DARPSID_BUILD_VST3=OFF \
  -DARPSID_BUILD_STANDALONE=OFF 2>&1 | tee "$PROOF/cmake-configure.txt"

cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)" 2>&1 | tee "$PROOF/cmake-build.txt"
ctest --test-dir "$BUILD" --output-on-failure 2>&1 | tee "$PROOF/ctest.txt"

if command -v auval >/dev/null 2>&1; then
  auval -strict -v "$AU_TYPE" "$AU_SUBTYPE" "$AU_MANUFACTURER" 2>&1 | tee "$PROOF/auval-strict.txt"
  grep -q "AU VALIDATION SUCCEEDED" "$PROOF/auval-strict.txt"
else
  echo "auval not found" | tee "$PROOF/auval-strict.txt"
  exit 1
fi

COMPONENT="$HOME/Library/Audio/Plug-Ins/Components/ArpSID.component"
if [[ -d "$COMPONENT" ]]; then
  codesign --verify --deep --strict --verbose=4 "$COMPONENT" 2>&1 | tee "$PROOF/codesign-component.txt"
  /usr/libexec/PlistBuddy -c 'Print :AudioComponents' "$COMPONENT/Contents/Info.plist" 2>&1 | tee "$PROOF/audio-components-plist.txt" || true
else
  echo "Missing installed component: $COMPONENT" | tee "$PROOF/codesign-component.txt"
  exit 1
fi

cat > "$PROOF/LOGIC_MANUAL_GATE_REQUIRED.md" <<'EOM'
# Required manual Logic Pro gate

The release is not signed off until a human records pass/fail evidence for:

- Logic Pro scans ArpSID without quarantine or validation failure.
- AUv2 UI opens.
- Factory preset/kit selection is audible.
- Stop→Play does not reset audible preset/kit to slot 0.
- Save project, close Logic, reopen project, audible state matches saved state.
- Offline bounce of a deterministic MIDI region is stable across two bounces.
- At least four ArpSID instances can play simultaneously without stuck notes or render errors.
- Automation storm on filter/drive/preset-safe parameters does not crash or produce stuck notes.
EOM

echo "macOS release gate complete. Proof: $PROOF"
