#!/usr/bin/env bash
set -euo pipefail
ROOT="${1:-$(pwd)}"
cd "$ROOT"
fail=0

if grep -R "canonicalApplyRegisterImage(sid_register, runtime->registerImage" -n include source; then
  echo "FAIL: render path still applies registerImage into sid_register" >&2
  fail=1
fi

if grep -R "file(GLOB AUV3_OBJCPP" -n CMakeLists.txt; then
  echo "FAIL: AUv3 target still uses file(GLOB)" >&2
  fail=1
fi

if ! grep -q "set(ARPSID_BUILD_VST3 ON CACHE BOOL" CMakeLists.txt; then
  echo "FAIL: BUILD_ALL no longer forces VST3" >&2
  fail=1
fi

if ! grep -q "auv2UpdateSilenceFlag" source/au2/ArpSIDAUv2Component.mm; then
  echo "FAIL: AUv2 silence flag truth hook missing" >&2
  fail=1
fi

if grep -q "critSlotReady_" include/arpsid/core/sid_ingress_lane.h; then
  echo "FAIL: old critical ready-flag ring remains" >&2
  fail=1
fi

if grep -q "buildTransportState(musCtx" source/au3/ArpSIDAudioUnit.mm; then
  echo "FAIL: AUv3 render host-block call remains" >&2
  fail=1
fi

if [[ $fail -ne 0 ]]; then
  exit $fail
fi

echo "PASS: P1 static guards"
