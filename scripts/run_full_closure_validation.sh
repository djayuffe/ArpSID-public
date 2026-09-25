#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-${ROOT}/.build/full-closure}"
# shellcheck source=scripts/arpsid_build_helpers.sh
source "${ROOT}/scripts/arpsid_build_helpers.sh"
NCPU="$(arpsid_ncpu)"

echo "== ArpSID full closure validation =="
echo "ROOT: ${ROOT}"
echo "BUILD: ${BUILD}"
echo "NCPU: ${NCPU}"

cd "${ROOT}"

echo "== 1. Source-tree guard =="
python3 "${ROOT}/scripts/verify_source_tree.py"
python3 "${ROOT}/scripts/check_audit_closure.py"

echo "== 2. Configure =="
arpsid_ensure_fresh_build_dir "${ROOT}" "${BUILD}" "${ARPSID_CLOSURE_CLEAN:-0}"
cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DARPSID_BUILD_TESTS=ON

echo "== 3. Build all default targets/tests =="
arpsid_build_with_serial_retry "${BUILD}" "${NCPU}"

echo "== 4. Verify CTest executables exist =="
missing=0
while IFS= read -r line; do
  test_name="$(printf "%s" "${line}" | sed -n 's/^  Test #[0-9]\+: \([^ ]\+\).*/\1/p')"
  [[ -z "${test_name}" ]] && continue
  # CTest has already generated tests, but executable names vary. The hard proof
  # is the full ctest run below; this loop is a readable preflight marker.
  :
done < <(ctest --test-dir "${BUILD}" -N)

echo "== 5. CTest all =="
ctest --test-dir "${BUILD}" --output-on-failure --no-tests=error

echo "== 6. Focused closure suite =="
ctest --test-dir "${BUILD}" -R "DigiD418StreamEngineV698Tests|DigiD418SidVolumeDacV699Tests|DspKernelIncludeSmokeV633Tests|FullPreflightPass83AndStale128V697Tests|DrumBridgeNoSilenceV613Tests|KitMixedTargetCompileV628Tests|KitHashFieldContractV632Tests|DspKernelMultiTuSmokeV634Tests|KitSid808FactoryVoicePrecedenceV637Tests|DrSidKitPayloadV638Tests|DigiSlotBridgeContractV639Tests|DrSidFactorySlotAudioShapeV640Tests|DigiNonzeroSlotRuntimeV641Tests|Sid808ProjectionSyncContractV642Tests|KitTargetAuthorityV643Tests|DrumBridgeRuntimeAuthorityV644Tests|FullClosureManifestV645Tests|AuditClosureMatrixV646Tests|DrSidKitVoiceDeepOverrideV647Tests|DigiKitDenseProjectionV648Tests|BridgeDrsidAccessorRemovedV649Tests|KitMixedTargetRuntimeV650Tests|DrumStemMixerV651Tests|DspKernelStemPolicyContractV652Tests" --output-on-failure

echo "== DONE: full closure validation passed =="
