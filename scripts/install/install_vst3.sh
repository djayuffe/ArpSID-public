#!/usr/bin/env bash
# ArpSID VST3 installer for Linux and macOS.
#
# Run it from an extracted release zip (arpsid_vst3.vst3 next to this script)
# or point it at a bundle with --from.
#
# usage: install.sh [--system] [--uninstall] [--check] [--from PATH] [--dest DIR] [--yes]
#   (default)     install for the current user
#                   Linux: ~/.vst3      macOS: ~/Library/Audio/Plug-Ins/VST3
#   --system      install for all users (uses sudo)
#                   Linux: /usr/lib/vst3   macOS: /Library/Audio/Plug-Ins/VST3
#   --dest DIR    install into DIR instead
#   --uninstall   remove ArpSID from the chosen location
#   --check       only check the bundle (Linux: shared libraries it needs)
#   --from PATH   the arpsid_vst3.vst3 bundle to install
#   --yes         do not ask before replacing an existing install
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE_NAME="arpsid_vst3.vst3"
FROM=""
DEST=""
SYSTEM=0
UNINSTALL=0
CHECK_ONLY=0
YES=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --system) SYSTEM=1; shift ;;
    --uninstall) UNINSTALL=1; shift ;;
    --check) CHECK_ONLY=1; shift ;;
    --from) FROM="${2:?--from needs a path}"; shift 2 ;;
    --dest) DEST="${2:?--dest needs a directory}"; shift 2 ;;
    --yes|-y) YES=1; shift ;;
    -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
    *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
  esac
done

OS="$(uname -s)"
case "$OS" in
  Linux)
    USER_DIR="$HOME/.vst3"; SYSTEM_DIR="/usr/lib/vst3" ;;
  Darwin)
    USER_DIR="$HOME/Library/Audio/Plug-Ins/VST3"; SYSTEM_DIR="/Library/Audio/Plug-Ins/VST3" ;;
  *)
    echo "This installer is for Linux and macOS; on Windows use install.ps1." >&2; exit 1 ;;
esac
[ -n "$DEST" ] || { [ "$SYSTEM" -eq 1 ] && DEST="$SYSTEM_DIR" || DEST="$USER_DIR"; }

SUDO=()
need_sudo() {
  local dir="$1"
  while [ ! -e "$dir" ]; do dir="$(dirname "$dir")"; done
  [ -w "$dir" ] && return 1
  return 0
}
if need_sudo "$DEST"; then
  command -v sudo >/dev/null 2>&1 || { echo "$DEST is not writable and sudo is not available." >&2; exit 1; }
  SUDO=(sudo)
fi

if [ "$UNINSTALL" -eq 1 ]; then
  if [ -e "$DEST/$BUNDLE_NAME" ]; then
    "${SUDO[@]}" rm -rf "$DEST/$BUNDLE_NAME"
    echo "Removed $DEST/$BUNDLE_NAME"
  else
    echo "ArpSID is not installed in $DEST"
  fi
  exit 0
fi

# Locate the bundle.
if [ -z "$FROM" ]; then
  for c in "$HERE/$BUNDLE_NAME" "$PWD/$BUNDLE_NAME"; do
    [ -d "$c" ] && { FROM="$c"; break; }
  done
fi
[ -n "$FROM" ] && [ -d "$FROM" ] || { echo "No $BUNDLE_NAME found (use --from PATH)." >&2; exit 1; }
FROM="$(cd "$FROM" && pwd)"

# Check: the module for this machine exists and (Linux) its libraries resolve.
check_bundle() {
  local ok=0
  if [ "$OS" = Linux ]; then
    local arch; arch="$(uname -m)"
    local so="$FROM/Contents/$arch-linux/arpsid_vst3.so"
    if [ ! -f "$so" ]; then
      echo "This bundle has no $arch-linux module (it contains: $(ls "$FROM/Contents" | tr '\n' ' '))." >&2
      return 1
    fi
    if command -v ldd >/dev/null 2>&1; then
      local missing
      missing="$(ldd "$so" 2>/dev/null | awk '/not found/ {print $1}' | sort -u)"
      if [ -n "$missing" ]; then
        ok=1
        echo "Missing shared libraries for ArpSID:" >&2
        echo "$missing" | sed 's/^/  /' >&2
        echo "Install the runtime packages, e.g." >&2
        echo "  Debian/Ubuntu: sudo apt-get install libxcb1 libxcb-util1 libxcb-cursor0 libxcb-keysyms1 libxcb-xkb1 libxkbcommon0 libxkbcommon-x11-0 libcairo2 libpango-1.0-0 libpangocairo-1.0-0 libfontconfig1 libfreetype6" >&2
        echo "  Fedora:        sudo dnf install libxcb xcb-util xcb-util-cursor xcb-util-keysyms libxkbcommon libxkbcommon-x11 cairo pango fontconfig freetype" >&2
        echo "  Arch:          sudo pacman -S libxcb xcb-util xcb-util-cursor xcb-util-keysyms libxkbcommon libxkbcommon-x11 cairo pango fontconfig freetype2" >&2
      else
        echo "All shared libraries ArpSID needs are present."
      fi
    fi
  else
    [ -d "$FROM/Contents/MacOS" ] || { echo "Not a macOS VST3 bundle: $FROM" >&2; return 1; }
    echo "Bundle architectures: $(lipo -archs "$FROM/Contents/MacOS/arpsid_vst3" 2>/dev/null || echo unknown)"
  fi
  return $ok
}

if [ "$CHECK_ONLY" -eq 1 ]; then
  check_bundle
  exit $?
fi
check_bundle || echo "(installing anyway; ArpSID will not load until the libraries above are installed)" >&2

if [ -e "$DEST/$BUNDLE_NAME" ] && [ "$YES" -eq 0 ] && [ -t 0 ]; then
  read -r -p "Replace the existing $DEST/$BUNDLE_NAME? [Y/n] " answer
  case "$answer" in n|N|no|NO) echo "Cancelled."; exit 0 ;; esac
fi

"${SUDO[@]}" mkdir -p "$DEST"
"${SUDO[@]}" rm -rf "$DEST/$BUNDLE_NAME"
"${SUDO[@]}" cp -R "$FROM" "$DEST/$BUNDLE_NAME"
if [ "$OS" = Darwin ]; then
  # Downloaded bundles carry the quarantine flag; the plug-in is ad-hoc signed.
  "${SUDO[@]}" xattr -dr com.apple.quarantine "$DEST/$BUNDLE_NAME" 2>/dev/null || true
fi
echo "Installed ArpSID VST3 to $DEST/$BUNDLE_NAME"
echo "Rescan plug-ins in your host (the plug-in is listed as 'ArpSID')."
