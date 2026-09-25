#!/usr/bin/env bash
# Create a clean release zip from a validated ArpSID source tree.
# Excludes build products, caches, editor cruft and generated archives so the
# shipped tree is reproducible and safe to unpack into a clean build directory.
set -Eeuo pipefail
ROOT_DIR="${1:-$(pwd)}"
ROOT_DIR="$(cd "${ROOT_DIR}" && pwd -P)"
RELEASE_NAME="${RELEASE_NAME:-$(basename "${ROOT_DIR}")}"
PACKAGE_OUT="${PACKAGE_OUT:-$(dirname "${ROOT_DIR}")/${RELEASE_NAME}.zip}"
if [[ ! -f "${ROOT_DIR}/CMakeLists.txt" || ! -f "${ROOT_DIR}/build.sh" ]]; then
  echo "ERROR: package root must contain CMakeLists.txt and build.sh: ${ROOT_DIR}" >&2
  exit 1
fi
if ! command -v zip >/dev/null 2>&1; then
  echo "ERROR: zip command is required for --package-release" >&2
  exit 2
fi
parent="$(dirname "${ROOT_DIR}")"
base="$(basename "${ROOT_DIR}")"
PACKAGE_TMP_ROOT="${PACKAGE_TMP_ROOT:-${TMPDIR:-/tmp}}"
mkdir -p "${PACKAGE_TMP_ROOT}"
tmp="${PACKAGE_TMP_ROOT%/}/.${base}.package.$$"
trap 'rm -rf "${tmp}"' EXIT
# v896: PACKAGE_OUT may point into a not-yet-existing directory (e.g. dist/);
# create it so the zip step cannot fail on a missing parent.
mkdir -p "$(dirname "${PACKAGE_OUT}")"
rm -rf "${tmp}" "${PACKAGE_OUT}"
mkdir -p "${tmp}/${RELEASE_NAME}"
# rsync is preferred for exact exclusions; fall back to tar if unavailable.
if command -v rsync >/dev/null 2>&1; then
  rsync -a --delete \
    --exclude '/build/' \
    --exclude '/build*/' \
    --exclude '/.build/' \
    --exclude '/dist/' \
    --exclude '/release-logs/' \
    --exclude '/cmake-build-*/' \
    --exclude '/CMakeFiles/' \
    --exclude '/CMakeCache.txt' \
    --exclude '/Testing/' \
    --exclude '/.git/' \
    --exclude '.claude/' \
    --exclude '.vscode/' \
    --exclude '.idea/' \
    --exclude '.DS_Store' \
    --exclude 'CMakeCache.txt' \
    --exclude '/*.zip' \
    --exclude '/*.patch' \
    --exclude '/RELEASE_MANIFEST_PASS*.sha256' \
    --exclude '/PASS*.md' \
    --exclude '/ArpSID_pass*_REPORT.md' \
    --exclude '/RELEASE_PASS*.md' \
    --exclude '/RELEASE_NOTES_PASS*.md' \
    --exclude '/docs/PASS*.md' \
    --exclude '/PASS*_SOURCE_TREE_MANIFEST.txt' \
    --exclude '/HANDOFF.md' \
    --exclude '/WARNING_FIX_PASS*.md' \
    "${ROOT_DIR}/" "${tmp}/${RELEASE_NAME}/"
else
  (cd "${ROOT_DIR}" && tar \
    --exclude='./build' \
    --exclude='./build*' \
    --exclude='./.build' \
    --exclude='./dist' \
    --exclude='./dist/*' \
    --exclude='./release-logs' \
    --exclude='./release-logs/*' \
    --exclude='./cmake-build-*' \
    --exclude='./CMakeFiles' \
    --exclude='./CMakeCache.txt' \
    --exclude='./Testing' \
    --exclude='./.git' \
    --exclude='.claude' \
    --exclude='.claude/*' \
    --exclude='.vscode' \
    --exclude='.vscode/*' \
    --exclude='.idea' \
    --exclude='.idea/*' \
    --exclude='.DS_Store' \
    --exclude='*/.DS_Store' \
    --exclude='CMakeCache.txt' \
    --exclude='*/CMakeCache.txt' \
    --exclude='./*.zip' \
    --exclude='./*.patch' \
    --exclude='./RELEASE_MANIFEST_PASS*.sha256' \
    --exclude='./PASS*.md' \
    --exclude='./ArpSID_pass*_REPORT.md' \
    --exclude='./RELEASE_PASS*.md' \
    --exclude='./RELEASE_NOTES_PASS*.md' \
    --exclude='./docs/PASS*.md' \
    --exclude='./PASS*_SOURCE_TREE_MANIFEST.txt' \
    --exclude='./HANDOFF.md' \
    --exclude='./WARNING_FIX_PASS*.md' \
    -cf - .) | (cd "${tmp}/${RELEASE_NAME}" && tar -xf -)
fi
manifest="${tmp}/${RELEASE_NAME}/RELEASE_CONTENTS.sha256"
if command -v sha256sum >/dev/null 2>&1; then
  (cd "${tmp}/${RELEASE_NAME}" && find . -type f ! -name 'RELEASE_CONTENTS.sha256' -print0 | sort -z | xargs -0 sha256sum > "${manifest}")
elif command -v shasum >/dev/null 2>&1; then
  (cd "${tmp}/${RELEASE_NAME}" && find . -type f ! -name 'RELEASE_CONTENTS.sha256' -print0 | sort -z | xargs -0 shasum -a 256 > "${manifest}")
else
  echo "ERROR: sha256sum or shasum is required for release content manifest" >&2
  exit 3
fi
(cd "${tmp}" && zip -qr "${PACKAGE_OUT}" "${RELEASE_NAME}")
if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "${PACKAGE_OUT}"
elif command -v shasum >/dev/null 2>&1; then
  shasum -a 256 "${PACKAGE_OUT}"
fi
echo "PACKAGED ${PACKAGE_OUT}"
