#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."

fail=0

if grep -R "file(GLOB AUV3_OBJCPP" -n CMakeLists.txt; then
  echo "FAIL: AUv3 target uses nondeterministic file(GLOB) membership" >&2
  fail=1
fi

if grep -n "source/au3/ArpSIDDSPKernel_process.cpp" CMakeLists.txt; then
  echo "FAIL: stale empty ArpSIDDSPKernel_process.cpp is still compiled or has source-file properties" >&2
  fail=1
fi

env -i PATH=/usr/bin:/bin python3 -S - <<'PY' || fail=1
from pathlib import Path
s = Path('source/au3/ArpSIDDSPKernel.hpp').read_text()
start = s.index('    Telemetry readTelemetry() const noexcept {')
end = s.index('    // Copies up to maxSamples', start)
body = s[start:end]
for forbidden in ['runtimeModel_.', 'runtimeHostSurface_()', 'renderParams_', 'lfos_()->', 'forEachActiveTokenVoice']:
    if forbidden in body:
        raise SystemExit(f'FAIL: readTelemetry still reads live mutable runtime: {forbidden}')
required = [
    'telemetryHostTempo_', 'telemetrySeqStep_', 'telemetryActiveTokenCount_',
    'telemetryTokens_', 'telemetryEnv1Level_', 'telemetryRandomValue_',
    'telemetryParamSnapshot'
]
for needle in required:
    if needle not in body:
        raise SystemExit(f'FAIL: readTelemetry missing snapshot field: {needle}')
print('PASS: readTelemetry uses published telemetry snapshot only')
PY

if [[ "$fail" -ne 0 ]]; then
  exit 1
fi

echo "PASS: P2 static guards"
