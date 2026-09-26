#!/usr/bin/env bash
# Fail unless the tree at $1 (default: repo root) is safe to publish: the C64
# ROM header must be the zero-filled public placeholder, never the real
# Commodore KERNAL/BASIC/CHARGEN images that live only in the private repo.
set -euo pipefail

ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
ROMS="${ROOT}/include/arpsid/core/c64_embedded_roms.h"
# The placeholder is ~2 KB; the real ROM images are ~100 KB of byte arrays.
MAX_STUB_BYTES=16384

fail() { echo "check_public_tree: FAIL: $*" >&2; exit 1; }

[ -f "$ROMS" ] || fail "missing $ROMS"
grep -q 'kEmbeddedC64RomsAvailable = false' "$ROMS" \
  || fail "c64_embedded_roms.h does not declare kEmbeddedC64RomsAvailable = false (real ROMs?)"
size="$(wc -c < "$ROMS" | tr -d ' ')"
[ "$size" -le "$MAX_STUB_BYTES" ] \
  || fail "c64_embedded_roms.h is ${size} bytes (> ${MAX_STUB_BYTES}); looks like real ROM data"

echo "check_public_tree: OK (C64 ROM header is the public placeholder)"
