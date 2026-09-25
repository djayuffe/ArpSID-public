#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-${ROOT}/.build/full-ctest-preflight}"
# shellcheck source=scripts/arpsid_build_helpers.sh
source "${ROOT}/scripts/arpsid_build_helpers.sh"
NCPU="${ARPSID_PREFLIGHT_JOBS:-$(arpsid_ncpu)}"
LOG_DIR="${ARPSID_PREFLIGHT_LOG_DIR:-${BUILD}/preflight-logs}"
arpsid_ensure_fresh_build_dir "${ROOT}" "${BUILD}" "${ARPSID_PREFLIGHT_CLEAN:-0}"
mkdir -p "${LOG_DIR}"

log_run() {
  local name="$1"; shift
  echo "== ${name} =="
  echo "+ $*"
  "$@" 2>&1 | tee "${LOG_DIR}/${name// /_}.log"
}

echo "== ArpSID full CTest preflight =="
echo "ROOT: ${ROOT}"
echo "BUILD: ${BUILD}"
echo "NCPU: ${NCPU}"
echo "LOG_DIR: ${LOG_DIR}"

cd "${ROOT}"

log_run "01 source tree guard" python3 scripts/verify_source_tree.py
log_run "02 audit closure guard" python3 scripts/check_audit_closure.py
log_run "03 configure release" cmake -S "${ROOT}" -B "${BUILD}" -DCMAKE_BUILD_TYPE=Release -DARPSID_BUILD_TESTS=ON

# Build all default/test targets. If a high-parallel build fails, retry once with
# -j1 so the first real compiler error is easy to read in CI/macOS logs.
set +e
cmake --build "${BUILD}" -j"${NCPU}" 2>&1 | tee "${LOG_DIR}/04_build_all_parallel.log"
BUILD_RC=${PIPESTATUS[0]}
set -e
if [[ ${BUILD_RC} -ne 0 ]]; then
  echo "WARN: parallel build failed; retrying serial build for deterministic first error" >&2
  cmake --build "${BUILD}" -j1 2>&1 | tee "${LOG_DIR}/04b_build_all_serial_retry.log"
fi

log_run "05 ctest inventory" ctest --test-dir "${BUILD}" -N
log_run "06 DIGI D418 contract ctest" ctest --test-dir "${BUILD}" -R "DigiD418StreamEngineV698Tests|DigiD418SidVolumeDacV699Tests|DspKernelIncludeSmokeV633Tests|FullPreflightPass83AndStale128V697Tests" --output-on-failure
log_run "07 full ctest" ctest --test-dir "${BUILD}" --output-on-failure --no-tests=error

log_run "08 closure sentinel suite" ctest --test-dir "${BUILD}" -R "FactorySlotEncodingRoundtripV665Tests|FactoryNoLegacy127AliasV669Tests|DrumContextSeparationV520Tests|ForensicEngineSanityV527Tests|FactoryDigiPayloadProjectionV691Tests|FactoryDrSidMicroprogramPayloadV692Tests|DrumBridgeDigiPolicyV693Tests|NoDead127BankSlotDecodeV694Tests|FactoryPayloadFinalDeepGuardV695Tests|VoicePolicyAllNotesOffChannelBoundsV696Tests|DigiD418StreamEngineV698Tests|DigiD418SidVolumeDacV699Tests|DspKernelIncludeSmokeV633Tests|FullPreflightPass83AndStale128V697Tests" --output-on-failure

echo "== DONE: full CTest preflight passed =="
