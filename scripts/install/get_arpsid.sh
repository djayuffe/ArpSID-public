#!/usr/bin/env bash
# Downloads an ArpSID release for this machine, verifies it against the
# release's SHA256SUMS.txt, and installs it.
#
#   curl -fsSL https://raw.githubusercontent.com/djayuffe/ArpSID-public/main/scripts/install/get_arpsid.sh | bash
#   curl -fsSL .../get_arpsid.sh | bash -s -- --version 0.9.8 --system
#
# usage: get_arpsid.sh [--version X.Y.Z] [--products LIST] [--keep] [installer options...]
#   --version    release to install (default: the latest)
#   --products   macOS only: comma list of vst3,auv2,auv3,standalone (default: all)
#   --keep       keep the downloaded files (printed at the end)
#   other options go to the installer (--system, --dest DIR, --yes, --validate, ...)
#
# Linux gets the VST3 (x86_64 or aarch64); macOS gets the universal AUv2,
# AUv3 app, Standalone app and VST3. On Windows use get_arpsid.ps1.
set -euo pipefail

REPO="${ARPSID_REPO:-djayuffe/ArpSID-public}"
# Installer fallback source, pinned to the release being installed (v$VERSION).
# Overridable for testing, but the default is the release ref — never an
# unpinned branch.
RAW_BASE="${ARPSID_RAW_BASE:-https://raw.githubusercontent.com/$REPO/v}"
VERSION=""
PRODUCTS="vst3,auv2,auv3,standalone"
KEEP=0
PASS=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --version) VERSION="${2:?--version needs X.Y.Z}"; VERSION="${VERSION#v}"; shift 2 ;;
    --products) PRODUCTS="${2:?--products needs a list}"; shift 2 ;;
    --keep) KEEP=1; shift ;;
    -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
    *) PASS+=("$1"); shift ;;
  esac
done

need() { command -v "$1" >/dev/null 2>&1 || { echo "get_arpsid: '$1' is required" >&2; exit 1; }; }
need curl
need unzip

if [ -z "$VERSION" ]; then
  VERSION="$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" |
             sed -n 's/.*"tag_name": *"v\{0,1\}\([^"]*\)".*/\1/p' | head -n 1)"
  [ -n "$VERSION" ] || { echo "get_arpsid: could not find the latest release of $REPO" >&2; exit 1; }
fi

OS="$(uname -s)"
ASSETS=()
case "$OS" in
  Linux)
    case "$(uname -m)" in
      x86_64|amd64) ASSETS=("ArpSID-$VERSION-vst3-linux-x86_64.zip") ;;
      aarch64|arm64) ASSETS=("ArpSID-$VERSION-vst3-linux-aarch64.zip") ;;
      *) echo "get_arpsid: no Linux build for $(uname -m) (x86_64 and aarch64 are published)" >&2; exit 1 ;;
    esac ;;
  Darwin)
    IFS=',' read -r -a want <<<"$PRODUCTS"
    for p in "${want[@]}"; do
      case "$p" in
        vst3) ASSETS+=("ArpSID-$VERSION-vst3-macos-universal.zip") ;;
        auv2) ASSETS+=("ArpSID-$VERSION-auv2-macos-universal.zip") ;;
        auv3) ASSETS+=("ArpSID-$VERSION-auv3-macos-universal.zip") ;;
        standalone) ASSETS+=("ArpSID-$VERSION-standalone-macos-universal.zip") ;;
        *) echo "get_arpsid: unknown product '$p'" >&2; exit 2 ;;
      esac
    done ;;
  *) echo "get_arpsid: use get_arpsid.ps1 on Windows" >&2; exit 1 ;;
esac

WORK="$(mktemp -d "${TMPDIR:-/tmp}/arpsid-install.XXXXXX")"
[ "$KEEP" -eq 1 ] || trap 'rm -rf "$WORK"' EXIT
BASE="https://github.com/$REPO/releases/download/v$VERSION"
echo "ArpSID $VERSION from $REPO"
curl -fsSL -o "$WORK/SHA256SUMS.txt" "$BASE/SHA256SUMS.txt"
for a in "${ASSETS[@]}"; do
  echo "  downloading $a"
  curl -fL --progress-bar -o "$WORK/$a" "$BASE/$a"
done

# Verify every downloaded zip against the release checksums.
(
  cd "$WORK"
  for a in "${ASSETS[@]}"; do
    expected="$(awk -v f="$a" '$2 == f || $2 == "*"f {print $1}' SHA256SUMS.txt)"
    [ -n "$expected" ] || { echo "get_arpsid: $a is not listed in SHA256SUMS.txt" >&2; exit 1; }
    if command -v sha256sum >/dev/null 2>&1; then actual="$(sha256sum "$a" | awk '{print $1}')"
    else actual="$(shasum -a 256 "$a" | awk '{print $1}')"; fi
    [ "$expected" = "$actual" ] || { echo "get_arpsid: checksum mismatch for $a" >&2; exit 1; }
    echo "  verified $a"
  done
)

mkdir -p "$WORK/extract"
for a in "${ASSETS[@]}"; do unzip -qo "$WORK/$a" -d "$WORK/extract"; done

# Releases from 0.9.8 carry their installer; older ones use the repository copy.
# The fallback installer is pinned to the release ref (RAW_BASE already ends in
# "v$VERSION") and, when the release publishes an installer checksum, verified
# against it before being executed. This prevents an unpinned main-branch
# installer from running against an old release.
installer_checksum() {
  # $1 = installer filename; echoes the expected sha256 or nothing.
  local f="$1"
  [ -f "$WORK/SHA256SUMS.txt" ] || return 0
  awk -v f="$f" '$2 == f || $2 == "*"f {print $1}' "$WORK/SHA256SUMS.txt"
}
verify_installer() {
  local path="$1" name="$2" expected actual
  expected="$(installer_checksum "$name")"
  if [ -z "$expected" ]; then
    # The installer is already pinned to the release ref (v$VERSION), so this is
    # defense-in-depth. Older releases may not list the installer .sh in
    # SHA256SUMS; warn loudly but continue rather than break the install.
    echo "get_arpsid: WARNING: no checksum for $name in SHA256SUMS.txt; using release-ref-pinned installer without verification" >&2
    return 0
  fi
  if command -v sha256sum >/dev/null 2>&1; then actual="$(sha256sum "$path" | awk '{print $1}')"
  else actual="$(shasum -a 256 "$path" | awk '{print $1}')"; fi
  [ "$expected" = "$actual" ] || { echo "get_arpsid: checksum mismatch for $name" >&2; exit 1; }
  echo "  verified $name"
}

INSTALLER=""
if [ "$OS" = Darwin ]; then
  [ -f "$WORK/extract/install_macos.sh" ] && INSTALLER="$WORK/extract/install_macos.sh"
  [ -n "$INSTALLER" ] || { INSTALLER="$WORK/install_macos.sh"; curl -fsSL -o "$INSTALLER" \
      "${RAW_BASE}${VERSION}/scripts/install/install_macos.sh"; verify_installer "$INSTALLER" "install_macos.sh"; }
  bash "$INSTALLER" --from "$WORK/extract" "${PASS[@]}"
else
  [ -f "$WORK/extract/install.sh" ] && INSTALLER="$WORK/extract/install.sh"
  [ -n "$INSTALLER" ] || { INSTALLER="$WORK/install.sh"; curl -fsSL -o "$INSTALLER" \
      "${RAW_BASE}${VERSION}/scripts/install/install_vst3.sh"; verify_installer "$INSTALLER" "install_vst3.sh"; }
  bash "$INSTALLER" --from "$WORK/extract/arpsid_vst3.vst3" "${PASS[@]}"
fi
[ "$KEEP" -eq 1 ] && echo "Downloads kept in $WORK"
exit 0
