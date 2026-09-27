#!/usr/bin/env bash
# Clones the Steinberg VST3 SDK at the version ArpSID is built and tested
# against (with its submodules, shallow) and prints the directory.
# Running it again on an existing checkout of the right tag does nothing.
#
# usage: scripts/fetch_vst3_sdk.sh [dir]     (default: <repo>/.deps/vst3sdk)
#   VST3SDK_TAG  overrides the pinned tag (the tag CI uses is below)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="${VST3SDK_TAG:-v3.8.1_build_84}"
DIR="${1:-$ROOT/.deps/vst3sdk}"

if [ -f "$DIR/CMakeLists.txt" ] && [ -d "$DIR/public.sdk" ] && [ -d "$DIR/vstgui4/vstgui" ]; then
  have="$(git -C "$DIR" describe --tags --exact-match 2>/dev/null || true)"
  if [ -z "$have" ] || [ "$have" = "$TAG" ]; then
    echo "$DIR"
    exit 0
  fi
  echo "[ArpSID] $DIR is at $have, expected $TAG; remove it or pass another directory." >&2
  exit 1
fi

mkdir -p "$(dirname "$DIR")"
echo "[ArpSID] cloning VST3 SDK $TAG into $DIR" >&2
git clone --depth 1 --branch "$TAG" --recurse-submodules --shallow-submodules \
  https://github.com/steinbergmedia/vst3sdk.git "$DIR" >&2
echo "$DIR"
