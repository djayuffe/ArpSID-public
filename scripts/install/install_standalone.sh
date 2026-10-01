#!/usr/bin/env bash
# ArpSID standalone app installer for Linux.
#
# Run it from an extracted release zip (the ArpSID program next to this
# script) or point it at the program with --from.
#
# usage: install.sh [--system] [--uninstall] [--check] [--from PATH] [--prefix DIR] [--yes]
#   (default)     install for the current user: ~/.local/bin/arpsid and a
#                 menu entry in ~/.local/share/applications
#   --system      install for all users: /usr/local/bin/arpsid and
#                 /usr/local/share/applications (uses sudo)
#   --prefix DIR  install into DIR/bin and DIR/share/applications instead
#   --uninstall   remove ArpSID from the chosen location (settings and the
#                 saved session in ~/.config/ArpSID are kept)
#   --check       only check that the libraries ArpSID needs are present
#   --from PATH   the ArpSID program to install
#   --yes         do not ask before replacing an existing install
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FROM=""
PREFIX=""
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
    --prefix) PREFIX="${2:?--prefix needs a directory}"; shift 2 ;;
    --yes|-y) YES=1; shift ;;
    -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
    *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
  esac
done

if [ "$(uname -s)" != "Linux" ]; then
  echo "This installer is for Linux (macOS: use install_macos.sh from the macOS zip)." >&2
  exit 1
fi

if [ -z "$PREFIX" ]; then
  if [ "$SYSTEM" -eq 1 ]; then PREFIX="/usr/local"; else PREFIX="$HOME/.local"; fi
fi
BIN_DIR="$PREFIX/bin"
APP_DIR="$PREFIX/share/applications"
TARGET="$BIN_DIR/arpsid"
DESKTOP="$APP_DIR/arpsid.desktop"

SUDO=""
if [ "$SYSTEM" -eq 1 ] && [ "$(id -u)" -ne 0 ]; then
  command -v sudo >/dev/null 2>&1 || { echo "--system needs root or sudo" >&2; exit 1; }
  SUDO="sudo"
fi

if [ "$UNINSTALL" -eq 1 ]; then
  $SUDO rm -f "$TARGET" "$DESKTOP"
  command -v update-desktop-database >/dev/null 2>&1 && $SUDO update-desktop-database -q "$APP_DIR" 2>/dev/null || true
  echo "Removed ArpSID from $PREFIX (settings in ~/.config/ArpSID are kept)."
  exit 0
fi

[ -n "$FROM" ] || FROM="$HERE/ArpSID"
if [ ! -f "$FROM" ]; then
  echo "ArpSID program not found at $FROM (use --from PATH)" >&2
  exit 1
fi

# Shared libraries the program needs (X11/xcb, cairo/pango, ALSA, ...).
missing="$(ldd "$FROM" 2>/dev/null | awk '/not found/ {print $1}' | sort -u || true)"
if [ -n "$missing" ]; then
  echo "Missing libraries:"
  echo "$missing" | sed 's/^/  /'
  echo "On Debian/Ubuntu: sudo apt-get install libxcb-util1 libxcb-cursor0 libxcb-keysyms1 libxkbcommon-x11-0 \\"
  echo "  libcairo2 libpangocairo-1.0-0 libfontconfig1 libfreetype6 libasound2t64 libpulse0"
  [ "$CHECK_ONLY" -eq 1 ] && exit 1
else
  echo "All shared libraries ArpSID needs are present."
fi
[ "$CHECK_ONLY" -eq 1 ] && exit 0

if [ -e "$TARGET" ] && [ "$YES" -ne 1 ] && [ -t 0 ]; then
  printf 'Replace the existing %s? [y/N] ' "$TARGET"
  read -r answer
  case "$answer" in y|Y|yes) ;; *) echo "Cancelled."; exit 1 ;; esac
fi

$SUDO mkdir -p "$BIN_DIR" "$APP_DIR"
$SUDO install -m 0755 "$FROM" "$TARGET"
tmp="$(mktemp)"
cat >"$tmp" <<EOF
[Desktop Entry]
Type=Application
Name=ArpSID
GenericName=SID Synthesizer
Comment=Commodore 64 SID synthesizer (standalone)
Exec=$TARGET
Terminal=false
Categories=AudioVideo;Audio;Music;Midi;
Keywords=synth;sid;c64;chiptune;midi;
StartupWMClass=ArpSID
EOF
$SUDO install -m 0644 "$tmp" "$DESKTOP"
rm -f "$tmp"
command -v update-desktop-database >/dev/null 2>&1 && $SUDO update-desktop-database -q "$APP_DIR" 2>/dev/null || true

echo "Installed ArpSID to $TARGET"
case ":$PATH:" in
  *":$BIN_DIR:"*) ;;
  *) echo "Note: $BIN_DIR is not on your PATH; start it from the menu or as $TARGET" ;;
esac
echo "Start it from your application menu (ArpSID) or run: arpsid"
