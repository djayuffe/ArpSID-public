#!/usr/bin/env sh
# Compatibility wrapper: historical docs/users invoked scripts/install_auv2_component.sh.
# The canonical installer lives in scripts/macos/install_auv2_component.sh.
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
exec /bin/sh "$SCRIPT_DIR/macos/install_auv2_component.sh" "$@"
