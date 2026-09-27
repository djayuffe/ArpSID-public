#!/usr/bin/env bash
# Regenerates docs/screenshots/vst3-editor-<tab>.png: one image per tab of the
# Windows/Linux VST3 editor, rendered offscreen by arpsid_vst3_editor_snapshot
# with the real engine running (factory patch 001, a C3-G3-C4-E4 chord held).
#
# usage: scripts/update_editor_screenshots.sh [build-dir]
#   build-dir  a VST3 build tree configured with -DARPSID_BUILD_VST3=ON
#              (default: build-vst3)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-build-vst3}"
case "$BUILD" in /*) ;; *) BUILD="$ROOT/$BUILD" ;; esac

cmake --build "$BUILD" --target arpsid_vst3_editor_snapshot >/dev/null
tool="$(find "$BUILD" -type f \( -name arpsid_vst3_editor_snapshot -o -name arpsid_vst3_editor_snapshot.exe \) | head -n 1)"
[ -n "$tool" ] || { echo "arpsid_vst3_editor_snapshot not found under $BUILD" >&2; exit 1; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"$tool" "$tmp"

out="$ROOT/docs/screenshots"
mkdir -p "$out"
# editor_NN_<TAB NAME>.png -> vst3-editor-<tab-name>.png
for f in "$tmp"/editor_*.png; do
  name="$(basename "$f" .png)"
  name="${name#editor_??_}"
  slug="$(printf '%s' "$name" | tr 'A-Z' 'a-z' | sed -e 's/_\{1,\}/-/g' -e 's/-*$//')"
  cp "$f" "$out/vst3-editor-$slug.png"
  echo "docs/screenshots/vst3-editor-$slug.png"
done
