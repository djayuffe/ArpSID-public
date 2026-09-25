#!/usr/bin/env bash
set -euo pipefail
root="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
auv2="$root/source/au2/ArpSIDAUv2Component.mm"
kern="$root/source/au3/ArpSIDDSPKernel.hpp"
canon="$root/source/au3/ArpSIDCanonicalEvents.h"
model="$root/include/arpsid/core/sid_runtime_model.h"
cmake="$root/CMakeLists.txt"
render_body=$(awk '/static OSStatus componentRender\(/,/^}/' "$auv2")
render_code=$(perl -0777 -pe 's{/\*.*?\*/}{}gs; s{//[^\n]*}{}g' <<<"$render_body")
if grep -q 'shared_lock\|unique_lock\|internalRenderBlock\|objc_msgSend\|new \|delete \|malloc\|calloc\|realloc\|free' <<<"$render_code"; then
  echo "FAIL: AUv2 componentRender contains forbidden realtime operation" >&2
  exit 1
fi
if ! grep -q 'loadPublishedRenderBlock' <<<"$render_body"; then
  echo "FAIL: AUv2 render does not use published render block" >&2
  exit 1
fi
if grep -q '^[[:space:]]*register_image_ = parameter_register_image_' "$model"; then
  echo "FAIL: state restore still seeds live register image from parameter image" >&2
  exit 1
fi
if grep -q 'makeSidCycleOrderingStamp(ev.sample_offset' "$canon"; then
  echo "FAIL: wrapper canonical conversion still owns SID timing stamp" >&2
  exit 1
fi
if ! grep -q 'finalizeCanonicalTiming_' "$kern"; then
  echo "FAIL: kernel/runtime timing finalizer missing" >&2
  exit 1
fi
if grep -q 'file(GLOB AUV3' "$cmake"; then
  echo "FAIL: AUv3 target membership still uses GLOB" >&2
  exit 1
fi
if ! grep -q 'ARPSID_BUILD_VST3 ON' "$cmake"; then
  echo "FAIL: BUILD_ALL does not force VST3" >&2
  exit 1
fi
echo "PASS: release gate static guards"
