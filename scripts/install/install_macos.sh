#!/usr/bin/env bash
# ArpSID installer for macOS release downloads.
#
# Installs every ArpSID product found next to this script (or in --from DIR):
#   ArpSID.component          AUv2 (5 flavors)   -> ~/Library/Audio/Plug-Ins/Components
#   arpsid_vst3.vst3          VST3               -> ~/Library/Audio/Plug-Ins/VST3
#   ArpSID.app                AUv3 host app      -> /Applications (registers the AUv3)
#   ArpSID Standalone.app     Standalone         -> /Applications
# then removes the download quarantine (the bundles are ad-hoc signed, not
# notarized) and refreshes the Audio Unit cache so hosts see the new version.
#
# usage: install_macos.sh [--system] [--uninstall] [--from DIR] [--validate] [--yes]
#   --system     plug-ins for all users (/Library/Audio/Plug-Ins/...; uses sudo)
#   --uninstall  remove every ArpSID product from the chosen locations
#   --from DIR   folder with the extracted bundles (default: this script's folder)
#   --validate   run Apple's auval on the AUv2 flavors after installing
#   --yes        do not ask before replacing existing installs
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FROM="$HERE"
SYSTEM=0
UNINSTALL=0
VALIDATE=0
YES=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --system) SYSTEM=1; shift ;;
    --uninstall) UNINSTALL=1; shift ;;
    --from) FROM="${2:?--from needs a folder}"; shift 2 ;;
    --validate) VALIDATE=1; shift ;;
    --yes|-y) YES=1; shift ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
  esac
done
[ "$(uname -s)" = Darwin ] || { echo "install_macos.sh is for macOS." >&2; exit 1; }

if [ "$SYSTEM" -eq 1 ]; then
  PLUG="/Library/Audio/Plug-Ins"; SUDO=(sudo)
else
  PLUG="$HOME/Library/Audio/Plug-Ins"; SUDO=()
fi
APPS="/Applications"
APP_SUDO=()
[ -w "$APPS" ] || APP_SUDO=(sudo)

# name | destination folder | sudo array name
ITEMS=("ArpSID.component|$PLUG/Components|SUDO"
       "arpsid_vst3.vst3|$PLUG/VST3|SUDO"
       "ArpSID.app|$APPS|APP_SUDO"
       "ArpSID Standalone.app|$APPS|APP_SUDO")

run_as() { # run_as <array-name> cmd...
  local arr="$1"; shift
  if [ "$arr" = SUDO ]; then "${SUDO[@]}" "$@"; else "${APP_SUDO[@]}" "$@"; fi
}

refresh_au_cache() {
  # The registrar rebuilds its cache on next launch; hosts then see the new build.
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  rm -f "$HOME/Library/Caches/AudioUnitCache/com.apple.audiounits.cache" 2>/dev/null || true
}

if [ "$UNINSTALL" -eq 1 ]; then
  for it in "${ITEMS[@]}"; do
    IFS='|' read -r name dest who <<<"$it"
    if [ -e "$dest/$name" ]; then
      [ "$name" = "ArpSID.app" ] && pluginkit -r "$dest/$name/Contents/PlugIns/arpsid_auv3.appex" 2>/dev/null || true
      run_as "$who" rm -rf "$dest/$name"
      echo "Removed $dest/$name"
    fi
  done
  refresh_au_cache
  exit 0
fi

found=0
for it in "${ITEMS[@]}"; do
  IFS='|' read -r name dest who <<<"$it"
  src="$FROM/$name"
  [ -d "$src" ] || continue
  found=1
  if [ -e "$dest/$name" ] && [ "$YES" -eq 0 ] && [ -t 0 ]; then
    read -r -p "Replace $dest/$name? [Y/n] " answer
    case "$answer" in n|N|no|NO) echo "Skipped $name"; continue ;; esac
  fi
  run_as "$who" mkdir -p "$dest"
  run_as "$who" rm -rf "$dest/$name"
  run_as "$who" ditto "$src" "$dest/$name"
  run_as "$who" xattr -dr com.apple.quarantine "$dest/$name" 2>/dev/null || true
  echo "Installed $dest/$name"
  if [ "$name" = "ArpSID.app" ]; then
    # Register the AUv3 app extension without having to open the app first.
    pluginkit -a "$dest/$name/Contents/PlugIns/arpsid_auv3.appex" 2>/dev/null || true
  fi
done
[ "$found" -eq 1 ] || { echo "No ArpSID bundles found in $FROM (use --from DIR)." >&2; exit 1; }

refresh_au_cache
echo "Audio Unit cache refreshed. Restart your host (Logic: quit and reopen) to rescan."

if [ "$VALIDATE" -eq 1 ] && [ -d "$PLUG/Components/ArpSID.component" ]; then
  for sub in ArpS ArIn DrSD S808 C64P; do
    if auval -v aumu "$sub" ASID >/tmp/arpsid-auval-$sub.log 2>&1; then
      echo "auval aumu $sub ASID: PASS"
    else
      echo "auval aumu $sub ASID: FAIL (see /tmp/arpsid-auval-$sub.log)"
    fi
  done
fi
