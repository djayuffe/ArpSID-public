#!/bin/sh
# package_project_zip.sh — create a release-style source zip without generated build trees
#
# Usage:
#   package_project_zip.sh [project_root] [output_zip]
#
# Defaults:
#   project_root = repository root inferred from this script location
#   output_zip   = /tmp/<project-name>_YYYYMMDD.zip

set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
DEFAULT_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)"
PROJECT_ROOT="${1:-$DEFAULT_ROOT}"

if [ ! -d "$PROJECT_ROOT" ]; then
    echo "package_project_zip: project root not found: $PROJECT_ROOT" >&2
    exit 1
fi

PROJECT_NAME="$(basename "$PROJECT_ROOT")"
STAMP="$(date +%Y%m%d)"
OUTPUT_ZIP="${2:-/tmp/${PROJECT_NAME}_${STAMP}.zip}"

STAGE_PARENT="$(mktemp -d "/tmp/${PROJECT_NAME}_pkg.XXXXXX")"
cleanup() {
    rm -rf "$STAGE_PARENT"
}
trap cleanup EXIT INT TERM

STAGE_ROOT="$STAGE_PARENT/$PROJECT_NAME"

rsync -a \
  --exclude '.DS_Store' \
  --exclude '.git/' \
  --exclude 'build*/' \
  --exclude 'release-logs/' \
  --exclude 'cmake-build*/' \
  --exclude 'CMakeFiles/' \
  --exclude 'Testing/' \
  --exclude '*.component/' \
  --exclude '*.app/' \
  --exclude '*.appex/' \
  --exclude '*.dSYM/' \
  --exclude '*.o' \
  --exclude '*.obj' \
  --exclude 'CMakeCache.txt' \
  --exclude 'cmake_install.cmake' \
  --exclude 'compile_commands.json' \
  --exclude '*.zip' \
  "$PROJECT_ROOT/" "$STAGE_ROOT/"

rm -f "$OUTPUT_ZIP"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$STAGE_ROOT" "$OUTPUT_ZIP"

echo "package_project_zip: created $OUTPUT_ZIP"
