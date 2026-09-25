#!/usr/bin/env bash
set -euo pipefail
file="${1:-source/au2/ArpSIDAUv2Component.mm}"
if [[ ! -f "$file" ]]; then
  echo "missing file: $file" >&2
  exit 2
fi
body="$(awk '/static OSStatus componentRender\(/,/^}/ {print}' "$file")"
forbidden='shared_lock|unique_lock|std::mutex|activityMutex|internalRenderBlock|retainedAudioUnitForInstance|objc_|dispatch_|NSLog|malloc\(|calloc\(|realloc\(|free\(|new |delete |\[[A-Za-z_][A-Za-z0-9_]* '
if grep -En "$forbidden" <<<"$body"; then
  echo "FAIL: AUv2 componentRender contains realtime-forbidden tokens" >&2
  exit 1
fi
if ! grep -q 'loadPublishedRenderBlock' <<<"$body"; then
  echo "FAIL: AUv2 componentRender does not use the published render block" >&2
  exit 1
fi
echo "PASS: AUv2 componentRender P0 guard"
