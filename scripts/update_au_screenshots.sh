#!/usr/bin/env bash
# Regenerates docs/screenshots/au-editor-<tab>.png (every tab of the native
# Cocoa editor used by AUv2, AUv3, the Standalone app and the macOS VST3) and
# docs/screenshots/au-flavor-<name>.png (the landing page of each AU flavor).
#
# macOS only. Builds the AUv2, installs it for the current user, then renders
# with the real engine running (factory patch 001, a C3-G3-C4-E4 chord held).
#
# usage: scripts/update_au_screenshots.sh [build-dir]   (default: build-auv2)
#
# CI runs the same tool in the macos job and uploads the images as the
# "au-editor-screenshots" artifact.
set -euo pipefail
[ "$(uname -s)" = Darwin ] || { echo "update_au_screenshots.sh needs macOS" >&2; exit 1; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-build-auv2}"
case "$BUILD" in /*) ;; *) BUILD="$ROOT/$BUILD" ;; esac

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DARPSID_BUILD_AUV2=ON -DARPSID_CODESIGN_IDENTITY=- >/dev/null
cmake --build "$BUILD" --target arpsid_auv2 arpsid_au_editor_snapshot --parallel "$(sysctl -n hw.ncpu)" >/dev/null
comp="$(find "$BUILD" -maxdepth 4 -type d -name ArpSID.component -print -quit)"
sh "$ROOT/scripts/macos/install_auv2_component.sh" "$comp" "$HOME/Library/Audio/Plug-Ins/Components" -
killall -9 AudioComponentRegistrar 2>/dev/null || true

tool="$(find "$BUILD" -type f -name arpsid_au_editor_snapshot -perm -u+x -print -quit)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"$tool" "$tmp" --flavors

out="$ROOT/docs/screenshots"
mkdir -p "$out"
for f in "$tmp"/au-*.png; do
  sips --resampleWidth 1280 "$f" >/dev/null   # Retina captures are 2x
  cp "$f" "$out/"
  echo "  $out/$(basename "$f")"
done
