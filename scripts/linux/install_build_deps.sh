#!/usr/bin/env bash
# Installs what an ArpSID build needs on Linux: a C++17 compiler, CMake,
# Ninja, Git, pkg-config, and (for the VST3 editor) the X11/xcb, xkbcommon,
# cairo/pango, fontconfig/freetype and Wayland development packages that
# VSTGUI builds against, plus ALSA and PulseAudio for the standalone app.
#
# usage: scripts/linux/install_build_deps.sh [--dry-run] [--no-editor]
#   --dry-run    print the package-manager command instead of running it
#   --no-editor  core/test build only (skip the editor and standalone app packages)
#
# Supported: Debian/Ubuntu (apt), Fedora/RHEL (dnf), Arch/Manjaro (pacman),
# openSUSE (zypper). Other systems: install the equivalent of the Debian list.
set -euo pipefail

DRY=0
EDITOR=1
for a in "$@"; do
  case "$a" in
    --dry-run) DRY=1 ;;
    --no-editor) EDITOR=0 ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) echo "unknown option: $a" >&2; exit 2 ;;
  esac
done

if command -v apt-get >/dev/null 2>&1; then
  PM=apt
  BASE=(build-essential cmake ninja-build git pkg-config zip unzip ccache)
  GUI=(libx11-xcb-dev libxcb-util-dev libxcb-cursor-dev libxcb-keysyms1-dev libxcb-xkb-dev
       libxkbcommon-dev libxkbcommon-x11-dev libcairo2-dev libpango1.0-dev
       libfontconfig1-dev libfreetype-dev libwayland-dev wayland-protocols
       libasound2-dev libpulse-dev)
  INSTALL=(apt-get install -y)
  PRE=(apt-get update)
elif command -v dnf >/dev/null 2>&1; then
  PM=dnf
  BASE=(gcc-c++ make cmake ninja-build git pkgconf-pkg-config zip unzip ccache)
  GUI=(libX11-devel libxcb-devel xcb-util-devel xcb-util-cursor-devel xcb-util-keysyms-devel
       libxkbcommon-devel libxkbcommon-x11-devel cairo-devel pango-devel
       fontconfig-devel freetype-devel wayland-devel wayland-protocols-devel
       alsa-lib-devel pulseaudio-libs-devel)
  INSTALL=(dnf install -y)
  PRE=()
elif command -v pacman >/dev/null 2>&1; then
  PM=pacman
  BASE=(base-devel cmake ninja git pkgconf zip unzip ccache)
  GUI=(libx11 libxcb xcb-util xcb-util-cursor xcb-util-keysyms libxkbcommon libxkbcommon-x11
       cairo pango fontconfig freetype2 wayland wayland-protocols alsa-lib libpulse)
  INSTALL=(pacman -S --needed --noconfirm)
  PRE=()
elif command -v zypper >/dev/null 2>&1; then
  PM=zypper
  BASE=(gcc-c++ cmake ninja git pkg-config zip unzip ccache)
  GUI=(libX11-devel libxcb-devel xcb-util-devel xcb-util-cursor-devel xcb-util-keysyms-devel
       libxkbcommon-devel libxkbcommon-x11-devel cairo-devel pango-devel
       fontconfig-devel freetype2-devel wayland-devel wayland-protocols-devel
       alsa-devel libpulse-devel)
  INSTALL=(zypper --non-interactive install)
  PRE=()
else
  echo "No supported package manager found (apt, dnf, pacman, zypper)." >&2
  echo "Install a C++17 compiler, CMake >= 3.19, Ninja, Git, pkg-config and, for the editor," >&2
  echo "the X11/xcb, xkbcommon, cairo, pango, fontconfig, freetype, Wayland, ALSA and PulseAudio dev packages." >&2
  exit 1
fi

PKGS=("${BASE[@]}")
[ "$EDITOR" -eq 1 ] && PKGS+=("${GUI[@]}")

SUDO=()
[ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1 && SUDO=(sudo)

echo "[ArpSID] package manager: $PM"
if [ "$DRY" -eq 1 ]; then
  [ "${#PRE[@]}" -gt 0 ] && echo "${SUDO[*]} ${PRE[*]}"
  echo "${SUDO[*]} ${INSTALL[*]} ${PKGS[*]}"
  exit 0
fi
[ "${#PRE[@]}" -gt 0 ] && "${SUDO[@]}" "${PRE[@]}"
"${SUDO[@]}" "${INSTALL[@]}" "${PKGS[@]}"
echo "[ArpSID] build dependencies installed."
