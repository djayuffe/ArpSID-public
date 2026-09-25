#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 /path/to/ArpSID_v444_v605_audit_fixes_pass49_*.zip [work-dir]" >&2
  exit 2
fi

ZIP_PATH="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
WORK_PARENT="${2:-${HOME}/Downloads/ArpSID_pass49_cleanroom}"
SRC_PARENT="${WORK_PARENT}/src"
BUILD_DIR="${WORK_PARENT}/.build/auv2-release"

echo "== Clean room =="
echo "ZIP: ${ZIP_PATH}"
echo "WORK_PARENT: ${WORK_PARENT}"

rm -rf "${WORK_PARENT}"
mkdir -p "${SRC_PARENT}"

echo "== Unpack =="
ditto -x -k "${ZIP_PATH}" "${SRC_PARENT}"

ROOT_COUNT="$(find "${SRC_PARENT}" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')"
if [[ "${ROOT_COUNT}" != "1" ]]; then
  echo "ERROR: expected exactly one root folder inside ZIP, found ${ROOT_COUNT}" >&2
  find "${SRC_PARENT}" -mindepth 1 -maxdepth 1 -type d >&2 || true
  exit 1
fi

ROOT="$(find "${SRC_PARENT}" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
echo "ROOT: ${ROOT}"

cd "${ROOT}"
# shellcheck source=scripts/arpsid_build_helpers.sh
source "${ROOT}/scripts/arpsid_build_helpers.sh"

echo "== Verify source tree =="
python3 scripts/verify_source_tree.py

echo "== Configure AUv2 =="
cmake -S . -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DARPSID_BUILD_AUV2=ON \
  -DARPSID_BUILD_TESTS=ON

echo "== Build AUv2 =="
arpsid_build_with_serial_retry "${BUILD_DIR}" "$(arpsid_ncpu)" --target arpsid_auv2

echo "== Build release closure tests =="
arpsid_build_with_serial_retry "${BUILD_DIR}" "$(arpsid_ncpu)" --target arpsid_release_closure_suite
ctest --test-dir "${BUILD_DIR}" -R "DigiD418StreamEngineV698Tests|DigiD418SidVolumeDacV699Tests|DigiGuiPadAuditionV700Tests|DigiPanelModelV563Tests|DigiSampleBankV596Tests|GuiRealtimeProjectionV588Tests|ReleaseClosureV701Tests|ReleasePackagingV702Tests|ReleaseNoJunkPlaceholderV703Tests|ReleaseManifestV704Tests" --output-on-failure

echo "== Done =="
echo "Built component should be under:"
find "${BUILD_DIR}" -maxdepth 8 -type d -name "ArpSID.component" 2>/dev/null || true
