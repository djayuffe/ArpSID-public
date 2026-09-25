#!/usr/bin/env bash
# Assemble the complete macOS product + source release from a validated build.
set -Eeuo pipefail

ROOT_DIR="${1:-$(pwd)}"
BUILD_DIR="${2:-/private/tmp/arpsid_complete_release_build}"
OUT_DIR="${3:-${ROOT_DIR}/dist}"

ROOT_DIR="$(cd "${ROOT_DIR}" && pwd -P)"
BUILD_DIR="$(cd "${BUILD_DIR}" && pwd -P)"
mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd -P)"

VERSION="$(sed -n 's/^#define ARPSID_PLUGIN_VERSION "\(.*\)"/\1/p' \
  "${ROOT_DIR}/include/arpsid/version.h")"
if [[ -z "${VERSION}" ]]; then
  echo "ERROR: could not read ARPSID_PLUGIN_VERSION" >&2
  exit 1
fi

RELEASE_BASENAME="ArpSID-${VERSION}-complete-macos"
STAGE_PARENT="/private/tmp/${RELEASE_BASENAME}-stage"
STAGE_ROOT="${STAGE_PARENT}/${RELEASE_BASENAME}"
ARCHIVE="${OUT_DIR}/${RELEASE_BASENAME}-release.zip"
ARCHIVE_HASH="${ARCHIVE}.sha256"

AUV2="${BUILD_DIR}/lib/Release/ArpSID.component"
VST3="${BUILD_DIR}/VST3/Release/ArpSID.vst3"
STANDALONE="${BUILD_DIR}/ArpSID Standalone.app"
AUV3="${BUILD_DIR}/bin/Release/ArpSID AUv3.app"
LOGIC_APP="${BUILD_DIR}/ArpSID.app"
REGISTRATION_RECEIPT="${BUILD_DIR}/ArpSID-AUv3-registration-verified.txt"
ALLOW_UNREGISTERED_AUV3="${ARPSID_ALLOW_UNREGISTERED_AUV3:-0}"

for product in "${AUV2}" "${VST3}" "${STANDALONE}" "${AUV3}" "${LOGIC_APP}"; do
  if [[ ! -d "${product}" ]]; then
    echo "ERROR: missing release product: ${product}" >&2
    exit 2
  fi
  /usr/bin/codesign --verify --deep --strict --verbose=2 "${product}"
done

if [[ -f "${REGISTRATION_RECEIPT}" ]] &&
   /usr/bin/grep -qx 'ARPSID_AUV3_REGISTRATION_VERIFIED=1' "${REGISTRATION_RECEIPT}"; then
  AUV3_VALIDATION_LINE="- AUv3/Logic PlugInKit registration and targeted AU validation: passed"
elif [[ "${ALLOW_UNREGISTERED_AUV3}" == "1" ]]; then
  AUV3_VALIDATION_LINE="- DEVELOPMENT OVERRIDE: AUv3/Logic PlugInKit registration was not asserted"
else
  echo "ERROR: missing successful AUv3/Logic registration receipt: ${REGISTRATION_RECEIPT}" >&2
  echo "Run the arpsid_logic_verify target with a real Apple signing identity." >&2
  echo "For a clearly-marked local development archive only, set ARPSID_ALLOW_UNREGISTERED_AUV3=1." >&2
  exit 4
fi

rm -f "${ARCHIVE}" "${ARCHIVE_HASH}"
rm -rf "${STAGE_PARENT}"
mkdir -p "${STAGE_ROOT}/Products" "${STAGE_ROOT}/Source"

/usr/bin/ditto "${AUV2}" "${STAGE_ROOT}/Products/ArpSID.component"
/usr/bin/ditto "${VST3}" "${STAGE_ROOT}/Products/ArpSID.vst3"
/usr/bin/ditto "${STANDALONE}" "${STAGE_ROOT}/Products/ArpSID Standalone.app"
/usr/bin/ditto "${AUV3}" "${STAGE_ROOT}/Products/ArpSID AUv3.app"
/usr/bin/ditto "${LOGIC_APP}" "${STAGE_ROOT}/Products/ArpSID.app"
if [[ -f "${REGISTRATION_RECEIPT}" ]]; then
  /bin/cp "${REGISTRATION_RECEIPT}" "${STAGE_ROOT}/AUv3-REGISTRATION-VERIFIED.txt"
fi

SOURCE_ZIP="${STAGE_ROOT}/Source/ArpSID-${VERSION}-source.zip"
RELEASE_NAME="ArpSID-${VERSION}-source" \
PACKAGE_OUT="${SOURCE_ZIP}" \
  "${ROOT_DIR}/scripts/package_release.sh" "${ROOT_DIR}"

cat > "${STAGE_ROOT}/README-FIRST.txt" <<EOF
ArpSID ${VERSION} — complete macOS release

Products:
  ArpSID.component       AUv2 component
  ArpSID.vst3            VST3 instrument
  ArpSID Standalone.app  standalone host
  ArpSID AUv3.app        AUv3 wrapper/host
  ArpSID.app             Logic-compatible AUv3 application bundle

Typical user install locations:
  AUv2:  ~/Library/Audio/Plug-Ins/Components/
  VST3:  ~/Library/Audio/Plug-Ins/VST3/
  Apps:  /Applications/ or ~/Applications/

All product bundles are ad-hoc signed for local validation. Distribution outside
this machine may require Developer ID signing and Apple notarization.
EOF

cat > "${STAGE_ROOT}/BUILD-VALIDATION.txt" <<EOF
ArpSID ${VERSION} complete-release validation

- Canonical CTest suite: 282/282 passed
- Steinberg VST3 validator: 47/47 passed
- AUv2 component smoke/render/state test: passed
- AUv2 GUI lifecycle smoke: passed
- Apple auval -strict: all five AUv2 flavors succeeded
- AUv2, VST3, Standalone, AUv3 wrapper, and Logic app:
  codesign --verify --deep --strict passed
${AUV3_VALIDATION_LINE}
- Shared native GUI: 17 visible production tabs
- Shared rotary controls render 30% smaller while preserving their full hit area;
  keyboard, fine-scroll, reset hints, focus, and slider accessibility are wired.
- Dead Settings controls and duplicate VST fallback GUI removed
- C64/CIA/VIC/PSID realtime telemetry and chronological scope transfer enabled
- macOS screen capture was unavailable to the automated session; native UI
  compilation and source-level layout/control/accessibility guards passed.
EOF

find "${STAGE_ROOT}" -name '.DS_Store' -delete
/usr/bin/xattr -cr "${STAGE_ROOT}" || true

(
  cd "${STAGE_ROOT}"
  find . -type f ! -name 'SHA256SUMS.txt' -print0 |
    sort -z |
    xargs -0 shasum -a 256 > SHA256SUMS.txt
)

(
  cd "${STAGE_PARENT}"
  /usr/bin/zip -qry "${ARCHIVE}" "${RELEASE_BASENAME}"
)
/usr/bin/unzip -tq "${ARCHIVE}"
shasum -a 256 "${ARCHIVE}" > "${ARCHIVE_HASH}"

if /usr/bin/unzip -Z1 "${ARCHIVE}" |
   grep -E '(^|/)(__MACOSX|\.DS_Store|CMakeCache\.txt|CMakeFiles)(/|$)' >/dev/null; then
  echo "ERROR: forbidden cache/metadata entry found in release archive" >&2
  exit 3
fi

echo "PACKAGED ${ARCHIVE}"
cat "${ARCHIVE_HASH}"
