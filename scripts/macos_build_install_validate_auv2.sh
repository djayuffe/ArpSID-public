#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ARPSID_AUV2_BUILD_DIR:-${ROOT}/.build/auv2-release}"
COMPONENT_NAME="ArpSID.component"
USER_COMPONENT_DIR="${HOME}/Library/Audio/Plug-Ins/Components"
SYSTEM_COMPONENT_DIR="/Library/Audio/Plug-Ins/Components"
USER_COMPONENT="${USER_COMPONENT_DIR}/${COMPONENT_NAME}"
SYSTEM_COMPONENT="${SYSTEM_COMPONENT_DIR}/${COMPONENT_NAME}"

# shellcheck source=scripts/arpsid_build_helpers.sh
source "${ROOT}/scripts/arpsid_build_helpers.sh"

echo "== Verify source tree / prevent stale-folder build =="
echo "Current source root: ${ROOT}"
python3 "${ROOT}/scripts/verify_source_tree.py"
arpsid_ensure_fresh_build_dir "${ROOT}" "${BUILD}" "${ARPSID_AUV2_CLEAN:-0}"

echo "== Configure AUv2 release =="
cmake -S "${ROOT}" -B "${BUILD}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DARPSID_BUILD_AUV2=ON \
  -DARPSID_BUILD_TESTS=ON

echo "== CMake source-tree guard target =="
cmake --build "${BUILD}" --target arpsid_verify_source_tree

echo "== Build and run release closure suite =="
arpsid_build_with_serial_retry "${BUILD}" "$(arpsid_ncpu)" --target arpsid_release_closure_suite
ctest --test-dir "${BUILD}" -R "DigiD418StreamEngineV698Tests|DigiD418SidVolumeDacV699Tests|DigiGuiPadAuditionV700Tests|DigiPanelModelV563Tests|DigiSampleBankV596Tests|GuiRealtimeProjectionV588Tests|ReleaseClosureV701Tests|ReleasePackagingV702Tests|ReleaseNoJunkPlaceholderV703Tests|ReleaseManifestV704Tests" --output-on-failure

echo "== Build AUv2 =="
BUILD_OK_MARKER="${BUILD}/.arpsid_auv2_build_ok"
rm -f "${BUILD_OK_MARKER}"
arpsid_build_with_serial_retry "${BUILD}" "$(arpsid_ncpu)" --target arpsid_auv2
touch "${BUILD_OK_MARKER}"

resolve_built_component() {
  local root="$1"
  local name="$2"
  local direct="${root}/${name}"
  if [[ -d "${direct}/Contents" && -f "${direct}/Contents/Info.plist" ]]; then
    printf '%s\n' "${direct}"
    return 0
  fi
  local found
  found="$(find "${root}" -maxdepth 8 -type d -name "${name}" -print -quit 2>/dev/null || true)"
  if [[ -n "${found}" && -f "${found}/Contents/Info.plist" ]]; then
    printf '%s\n' "${found}"
    return 0
  fi
  return 1
}

BUILT_COMPONENT="$(resolve_built_component "${BUILD}" "${COMPONENT_NAME}" || true)"
if [[ ! -f "${BUILD_OK_MARKER}" || -z "${BUILT_COMPONENT}" ]]; then
  echo "ERROR: AUv2 build did not produce ${COMPONENT_NAME} under ${BUILD}; refusing install." >&2
  echo "Searched direct path and nested bundle dirs. Existing component candidates:" >&2
  find "${BUILD}" -maxdepth 8 -type d -name "${COMPONENT_NAME}" -print 2>/dev/null >&2 || true
  exit 1
fi
echo "Built AUv2 component: ${BUILT_COMPONENT}"

echo "== Remove duplicate installed ArpSID.component copies =="
mkdir -p "${USER_COMPONENT_DIR}"
rm -rf "${USER_COMPONENT}"
if [[ -d "${SYSTEM_COMPONENT}" ]]; then
  echo "Removing system duplicate: ${SYSTEM_COMPONENT}"
  sudo rm -rf "${SYSTEM_COMPONENT}"
fi

echo "== Install user AUv2 =="
"${ROOT}/scripts/macos/install_auv2_component.sh" "${BUILT_COMPONENT}" "${USER_COMPONENT_DIR}" "${ARPSID_CODESIGN_IDENTITY:--}"

echo "== Verify exactly one installed ArpSID.component =="
FOUND_COUNT="$(find "${HOME}/Library/Audio/Plug-Ins/Components" "/Library/Audio/Plug-Ins/Components" \
  -maxdepth 1 -name "${COMPONENT_NAME}" -type d 2>/dev/null | wc -l | tr -d ' ')"
if [[ "${FOUND_COUNT}" != "1" ]]; then
  echo "ERROR: expected exactly one ${COMPONENT_NAME}, found ${FOUND_COUNT}" >&2
  find "${HOME}/Library/Audio/Plug-Ins/Components" "/Library/Audio/Plug-Ins/Components" \
    -maxdepth 1 -name "${COMPONENT_NAME}" -type d 2>/dev/null >&2 || true
  exit 1
fi

echo "== Clear AU/Logic caches =="
ARPSID_AUV2_HARD_REFRESH=1 "${ROOT}/scripts/macos/refresh_auv2_component.sh" "${USER_COMPONENT}"

echo "== AU list =="
if command -v auval >/dev/null 2>&1; then
  auval -a | grep -i "ArpSID\|ArpS\|ASID" || true
  echo "== AU validation: all shipped AUv2 music-device flavors =="
  auval -strict -v aumu ArpS ASID
  auval -strict -v aumu ArIn ASID
  auval -strict -v aumu DrSD ASID
  auval -strict -v aumu S808 ASID
  auval -strict -v aumu C64P ASID
else
  echo "ERROR: auval not found; install Xcode Command Line Tools / AudioUnit validation tool." >&2
  exit 1
fi

echo "== Done =="
echo "Installed: ${USER_COMPONENT}"
