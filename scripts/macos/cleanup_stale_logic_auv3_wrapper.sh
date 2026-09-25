#!/usr/bin/env bash
# cleanup_stale_logic_auv3_wrapper.sh
#
# Logic can keep launching a stale ArpSID AUv3 wrapper app
# (CFBundleIdentifier com.arpsid.auv3wrapper) even when the AUv2
# ArpSID.component is the only intended install. That stale wrapper causes
# Logic to route through AUHostingService/InfoHelper and can surface as the
# generic "Audio Unit plug-in reported a problem" dialog.
#
# This script quarantines only installed ArpSID AUv3 wrapper app bundles, then
# refreshes LaunchServices and the per-user AU/Logic caches. It does not delete
# anything permanently.
set -euo pipefail

WRAPPER_ID="com.arpsid.auv3wrapper"
EXTENSION_ID="com.arpsid.auv3"
STAMP="$(date +%Y%m%d_%H%M%S)"
QUARANTINE="${ARPSID_STALE_WRAPPER_QUARANTINE:-/private/tmp/arpsid_stale_logic_auv3_wrapper_${STAMP}}"
LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"

usage() {
  cat <<EOF
Usage: $0 [--force-quit-logic]

Moves stale installed ArpSID AUv3 wrapper apps out of discovery paths and
refreshes AU/Logic caches. Root-owned /Applications bundles will prompt for an
administrator password through macOS.

Options:
  --force-quit-logic   terminate Logic if a normal quit does not complete
EOF
}

force_quit_logic=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --force-quit-logic)
      force_quit_logic=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "cleanup_stale_logic_auv3_wrapper: macOS only" >&2
  exit 1
fi

plist_value() {
  local plist="$1"
  local key="$2"
  /usr/libexec/PlistBuddy -c "Print :${key}" "$plist" 2>/dev/null || true
}

is_arpsid_wrapper_app() {
  local app="$1"
  local plist="${app}/Contents/Info.plist"
  [[ -f "$plist" ]] || return 1
  local bundle_id
  bundle_id="$(plist_value "$plist" CFBundleIdentifier)"
  [[ "$bundle_id" == "$WRAPPER_ID" ]] || return 1
  [[ -d "${app}/Contents/PlugIns" ]] || return 0
  if find "${app}/Contents/PlugIns" -maxdepth 2 -name Info.plist -print 2>/dev/null |
      while IFS= read -r p; do
        [[ "$(plist_value "$p" CFBundleIdentifier)" == "$EXTENSION_ID" ]] && exit 42
      done; then
    return 0
  else
    [[ $? -eq 42 ]] && return 0
  fi
  return 0
}

quote_for_osascript_shell() {
  # Single-quote for /bin/sh source embedded inside osascript.
  printf "'%s'" "${1//\'/\'\\\'\'}"
}

move_app_to_quarantine() {
  local app="$1"
  local rel="${app#/}"
  local dest="${QUARANTINE}/${rel}"
  local dest_parent
  dest_parent="$(dirname "$dest")"

  mkdir -p "$dest_parent"
  if [[ -d "$dest" ]]; then
    dest="${dest}.${STAMP}"
    dest_parent="$(dirname "$dest")"
    mkdir -p "$dest_parent"
  fi

  if [[ -x "$LSREGISTER" ]]; then
    "$LSREGISTER" -u "$app" >/dev/null 2>&1 || true
  fi
  /usr/bin/pluginkit -r "$app" >/dev/null 2>&1 || true

  if mv "$app" "$dest" 2>/dev/null; then
    echo "moved $app -> $dest"
    return 0
  fi

  local q_app q_dest q_parent
  q_app="$(quote_for_osascript_shell "$app")"
  q_dest="$(quote_for_osascript_shell "$dest")"
  q_parent="$(quote_for_osascript_shell "$dest_parent")"
  /usr/bin/osascript -e "do shell script \"mkdir -p ${q_parent} && mv ${q_app} ${q_dest}\" with administrator privileges"
  echo "moved $app -> $dest"
}

discover_candidates() {
  {
    [[ -d "/Applications/ArpSID.app" ]] && printf '%s\n' "/Applications/ArpSID.app"
    [[ -d "${HOME}/Applications/ArpSID.app" ]] && printf '%s\n' "${HOME}/Applications/ArpSID.app"
    /usr/bin/mdfind "kMDItemCFBundleIdentifier == \"${WRAPPER_ID}\"" 2>/dev/null || true
  } | awk '!seen[$0]++'
}

echo "== Locate stale ArpSID AUv3 wrapper apps =="
mkdir -p "$QUARANTINE"
manifest="${QUARANTINE}/manifest.txt"
: >"$manifest"
moved=0
while IFS= read -r app; do
  [[ -n "$app" && -d "$app" ]] || continue
  if is_arpsid_wrapper_app "$app"; then
    echo "$app" >>"$manifest"
    move_app_to_quarantine "$app"
    moved=$((moved + 1))
  fi
done < <(discover_candidates)
echo "stale wrappers moved: ${moved}"
echo "quarantine: ${QUARANTINE}"

echo "== Stop Logic/plugin helper processes =="
/usr/bin/osascript -e 'tell application "Logic Pro X" to quit' >/dev/null 2>&1 || true
for _ in {1..20}; do
  pgrep -x "Logic Pro X" >/dev/null || break
  sleep 0.5
done
if pgrep -x "Logic Pro X" >/dev/null; then
  if [[ "$force_quit_logic" == "1" ]]; then
    /usr/bin/killall "Logic Pro X" >/dev/null 2>&1 || true
  else
    echo "Logic is still running; rerun with --force-quit-logic if it is stuck behind the AU warning." >&2
  fi
fi

/usr/bin/killall AUHostingServiceXPC AUHostingServiceXPC_arrow AUHostingService AudioComponentRegistrar auvaltool pkd lsd 2>/dev/null || true

echo "== Move per-user AU/Logic caches aside =="
cache_dir="${QUARANTINE}/Caches"
mkdir -p "$cache_dir"
for p in \
  "${HOME}/Library/Caches/AudioUnitCache" \
  "${HOME}/Library/Caches/com.apple.audio.AUHostingService.arm64e" \
  "${HOME}/Library/Caches/com.apple.logic10" \
  "${HOME}/Library/Caches/com.apple.musicapps/Logic" \
  "${HOME}/Library/Caches/com.apple.audiounits.cache"; do
  if [[ -e "$p" ]]; then
    base="$(basename "$p")"
    dest="${cache_dir}/${base}"
    [[ -e "$dest" ]] && dest="${dest}.${STAMP}"
    mv "$p" "$dest"
    echo "moved $p -> $dest"
  fi
done

echo "== Refresh LaunchServices / PlugInKit =="
if [[ -x "$LSREGISTER" ]]; then
  "$LSREGISTER" -gc >/dev/null 2>&1 || true
  "$LSREGISTER" -r -domain local -domain system -domain user >/dev/null 2>&1 || true
fi
/usr/bin/killall AudioComponentRegistrar pkd lsd 2>/dev/null || true
sleep 1

echo "== Verify stale wrapper is gone =="
remaining="$(/usr/bin/mdfind "kMDItemCFBundleIdentifier == \"${WRAPPER_ID}\"" 2>/dev/null || true)"
if [[ -n "$remaining" ]]; then
  echo "WARNING: remaining ${WRAPPER_ID} paths:" >&2
  echo "$remaining" >&2
else
  echo "OK: no ${WRAPPER_ID} paths indexed"
fi

if /usr/bin/pluginkit -m -A -D -v -i "$EXTENSION_ID" 2>/dev/null | grep -v 'no matches' | grep . >/dev/null; then
  echo "WARNING: PlugInKit still reports ${EXTENSION_ID}" >&2
  /usr/bin/pluginkit -m -A -D -v -i "$EXTENSION_ID" >&2 || true
else
  echo "OK: PlugInKit has no ${EXTENSION_ID} matches"
fi

echo "Done. Restart Logic before testing ArpSID AUv2."
