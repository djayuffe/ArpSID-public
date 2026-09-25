#!/bin/sh
set -eu

SRC_APP="${1:?source app missing}"
DEST_DIR="${2:?destination dir missing}"
DEST_APP="$DEST_DIR/ArpSID.app"

if [ ! -d "$SRC_APP" ]; then
  echo "Source Logic app missing: $SRC_APP" >&2
  exit 1
fi

/bin/mkdir -p "$DEST_DIR"
/bin/rm -rf "$DEST_APP"
/usr/bin/ditto "$SRC_APP" "$DEST_APP"
/usr/bin/xattr -dr com.apple.quarantine "$DEST_APP" || true

echo "Installed: $DEST_APP"
