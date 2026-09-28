#!/usr/bin/env bash
# ArpSID VST3 installer for Linux and macOS.
#
# Run it from an extracted release zip (arpsid_vst3.vst3 next to this script)
# or point it at a bundle with --from.
#
# usage: install.sh [--system] [--uninstall] [--check] [--from PATH] [--dest DIR]
#                   [--presets-dest DIR] [--no-presets] [--yes]
#   (default)     install for the current user
#                   Linux: ~/.vst3      macOS: ~/Library/Audio/Plug-Ins/VST3
#                 and the factory presets (.vstpreset) to
#                   Linux: ~/.vst3/presets  macOS: ~/Library/Audio/Presets
#   --system      install for all users (uses sudo)
#                   Linux: /usr/lib/vst3, /usr/share/vst3/presets
#                   macOS: /Library/Audio/Plug-Ins/VST3, /Library/Audio/Presets
#   --dest DIR    install the plug-in into DIR instead
#   --presets-dest DIR  install the presets into DIR/Uber Sound Solutions/ArpSID
#   --no-presets  do not install the factory presets
#   --uninstall   remove ArpSID (and its factory presets) from the chosen location
#   --check       only check the bundle (Linux: shared libraries it needs)
#   --from PATH   the arpsid_vst3.vst3 bundle to install
#   --yes         do not ask before replacing an existing install
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE_NAME="arpsid_vst3.vst3"
# VST3 preset folders are <root>/<vendor>/<plug-in name>.
PRESET_SUBDIR="Uber Sound Solutions/ArpSID"
FROM=""
DEST=""
PRESETS_DEST=""
NO_PRESETS=0
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
    --presets-dest) PRESETS_DEST="${2:?--presets-dest needs a directory}"; shift 2 ;;
    --no-presets) NO_PRESETS=1; shift ;;
    --yes|-y) YES=1; shift ;;
    -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
    *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
  esac
done

OS="$(uname -s)"
case "$OS" in
  Linux)
    USER_DIR="$HOME/.vst3"; SYSTEM_DIR="/usr/lib/vst3"
    USER_PRESETS="$HOME/.vst3/presets"; SYSTEM_PRESETS="/usr/share/vst3/presets" ;;
  Darwin)
    USER_DIR="$HOME/Library/Audio/Plug-Ins/VST3"; SYSTEM_DIR="/Library/Audio/Plug-Ins/VST3"
    USER_PRESETS="$HOME/Library/Audio/Presets"; SYSTEM_PRESETS="/Library/Audio/Presets" ;;
  *)
    echo "This installer is for Linux and macOS; on Windows use install.ps1." >&2; exit 1 ;;
esac
[ -n "$DEST" ] || { [ "$SYSTEM" -eq 1 ] && DEST="$SYSTEM_DIR" || DEST="$USER_DIR"; }
[ -n "$PRESETS_DEST" ] || { [ "$SYSTEM" -eq 1 ] && PRESETS_DEST="$SYSTEM_PRESETS" || PRESETS_DEST="$USER_PRESETS"; }
PRESETS_TARGET="$PRESETS_DEST/$PRESET_SUBDIR"

need_sudo() {
  local dir="$1"
  while [ ! -e "$dir" ]; do dir="$(dirname "$dir")"; done
  [ -w "$dir" ] && return 1
  return 0
}
require_sudo() {
  command -v sudo >/dev/null 2>&1 || { echo "$1 is not writable and sudo is not available." >&2; exit 1; }
}
# Empty arrays are expanded as ${A[@]+"${A[@]}"}: macOS bash 3.2 treats a
# plain "${A[@]}" of an empty array as unbound under set -u.
SUDO=(); PSUDO=()
if need_sudo "$DEST"; then require_sudo "$DEST"; SUDO=(sudo); fi
if need_sudo "$PRESETS_DEST"; then require_sudo "$PRESETS_DEST"; PSUDO=(sudo); fi

if [ "$UNINSTALL" -eq 1 ]; then
  if [ -e "$DEST/$BUNDLE_NAME" ]; then
    ${SUDO[@]+"${SUDO[@]}"} rm -rf "$DEST/$BUNDLE_NAME"
    echo "Removed $DEST/$BUNDLE_NAME"
  else
    echo "ArpSID is not installed in $DEST"
  fi
  if [ "$NO_PRESETS" -eq 0 ] && [ -e "$PRESETS_TARGET" ]; then
    ${PSUDO[@]+"${PSUDO[@]}"} rm -rf "$PRESETS_TARGET"
    ${PSUDO[@]+"${PSUDO[@]}"} rmdir "$(dirname "$PRESETS_TARGET")" 2>/dev/null || true
    echo "Removed the factory presets from $PRESETS_TARGET"
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

${SUDO[@]+"${SUDO[@]}"} mkdir -p "$DEST"
${SUDO[@]+"${SUDO[@]}"} rm -rf "$DEST/$BUNDLE_NAME"
${SUDO[@]+"${SUDO[@]}"} cp -R "$FROM" "$DEST/$BUNDLE_NAME"
if [ "$OS" = Darwin ]; then
  # Downloaded bundles carry the quarantine flag; the plug-in is ad-hoc signed.
  ${SUDO[@]+"${SUDO[@]}"} xattr -dr com.apple.quarantine "$DEST/$BUNDLE_NAME" 2>/dev/null || true
fi
echo "Installed ArpSID VST3 to $DEST/$BUNDLE_NAME"

# Factory presets (.vstpreset), shipped next to the bundle in the release zip.
PRESETS_SRC=""
for c in "$HERE/VST3 Presets/$PRESET_SUBDIR" "$(dirname "$FROM")/VST3 Presets/$PRESET_SUBDIR"; do
  [ -d "$c" ] && { PRESETS_SRC="$c"; break; }
done
if [ "$NO_PRESETS" -eq 0 ] && [ -n "$PRESETS_SRC" ]; then
  ${PSUDO[@]+"${PSUDO[@]}"} rm -rf "$PRESETS_TARGET"
  ${PSUDO[@]+"${PSUDO[@]}"} mkdir -p "$PRESETS_TARGET"
  ${PSUDO[@]+"${PSUDO[@]}"} cp -R "$PRESETS_SRC/." "$PRESETS_TARGET/"
  echo "Installed $(find "$PRESETS_TARGET" -name '*.vstpreset' | wc -l | tr -d ' ') factory presets to $PRESETS_TARGET"
elif [ "$NO_PRESETS" -eq 0 ]; then
  echo "(no 'VST3 Presets' folder next to the bundle: factory presets not installed)"
fi
echo "Rescan plug-ins in your host (the plug-in is listed as 'ArpSID')."
