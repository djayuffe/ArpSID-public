#!/usr/bin/env bash
# Adds the matching installer and a short INSTALL.txt to a folder that is
# about to be zipped for release (CI packaging and build.sh --package-vst3).
#
# usage: stage_installer.sh <vst3|vst3-windows|macos> <stage-dir>
#   vst3          Linux VST3 zip: install.sh
#   vst3-windows  Windows VST3 zip: install.ps1
#   macos         macOS zips: install_macos.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KIND="${1:?kind: vst3, vst3-windows or macos}"
STAGE="${2:?stage directory}"
mkdir -p "$STAGE"
DOC="https://github.com/djayuffe/ArpSID-public/blob/main/docs/INSTALL.md"

case "$KIND" in
  vst3)
    cp "$HERE/install_vst3.sh" "$STAGE/install.sh"
    chmod +x "$STAGE/install.sh"
    cat >"$STAGE/INSTALL.txt" <<EOF
ArpSID VST3 for Linux

  ./install.sh            install for you (~/.vst3, presets in ~/.vst3/presets)
  ./install.sh --system   install for all users (/usr/lib/vst3 and
                          /usr/share/vst3/presets, uses sudo)
  ./install.sh --check    check that the libraries ArpSID needs are present
  ./install.sh --uninstall

Or copy arpsid_vst3.vst3 into ~/.vst3 and the contents of "VST3 Presets" into
~/.vst3/presets yourself. Then rescan plug-ins in your host. The 180 factory
patches appear in the host's preset browser under ArpSID (Bass, Drums, Keys,
Lead, Pad, ...) and in the plug-in's program list.
Full guide: $DOC
EOF
    ;;
  vst3-windows)
    cp "$HERE/install_vst3.ps1" "$STAGE/install.ps1"
    cat >"$STAGE/INSTALL.txt" <<EOF
ArpSID VST3 for Windows

In an elevated PowerShell (Run as administrator), in this folder:
  powershell -ExecutionPolicy Bypass -File .\\install.ps1
Only for you (no admin rights):
  powershell -ExecutionPolicy Bypass -File .\\install.ps1 -Scope User
Remove:  ... install.ps1 -Uninstall

Or copy arpsid_vst3.vst3 into C:\\Program Files\\Common Files\\VST3 (the
32-bit x86 zip: C:\\Program Files (x86)\\Common Files\\VST3 on 64-bit
Windows, where 32-bit hosts look) and the contents of "VST3 Presets" into
Documents\\VST3 Presets yourself. Then rescan
plug-ins in your host. The 180 factory patches appear in the host's preset
browser under ArpSID and in the plug-in's program list.
Full guide: $DOC
EOF
    ;;
  macos)
    cp "$HERE/install_macos.sh" "$STAGE/install_macos.sh"
    chmod +x "$STAGE/install_macos.sh"
    cat >"$STAGE/INSTALL.txt" <<EOF
ArpSID for macOS

In Terminal, in this folder:
  ./install_macos.sh              install what is in this folder (plug-ins for you,
                                  apps to /Applications, VST3 factory presets to
                                  ~/Library/Audio/Presets), clear the download
                                  quarantine and refresh the Audio Unit cache
  ./install_macos.sh --system     plug-ins for all users
  ./install_macos.sh --validate   also run auval on the AU
  ./install_macos.sh --uninstall

Then restart your host. The bundles are ad-hoc signed (not notarized).
Full guide: $DOC
EOF
    ;;
  *) echo "unknown kind: $KIND" >&2; exit 2 ;;
esac
