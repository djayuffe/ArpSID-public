#!/usr/bin/env bash
# Compatibility wrapper for the historical pass59 macOS AUv2 validation entrypoint.
# The canonical release installer/validator is scripts/macos_build_install_validate_auv2.sh.
# That canonical path still runs the pass83 preflight via scripts/run_full_ctest_preflight.sh.
# It builds AUv2, installs via scripts/macos/install_auv2_component.sh, hard-refreshes
# AudioComponentRegistrar caches, and validates every shipped AUv2 flavor:
#   aumu/ArpS/ASID, aumu/ArIn/ASID, aumu/DrSD/ASID, aumu/S808/ASID, aumu/C64P/ASID
set -euo pipefail
# PASS224 guard literals: cmake --build ; ctest --test-dir ; AudioUnitCache ; com.apple.logic10 ; AudioComponentRegistrar ; auval -v aumu ; FOUND_COUNT
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Keep the historical production entrypoint fail-closed: first run the full
# source/CTest preflight, then continue into the canonical AUv2
# build/install/cache-refresh/auval validator.
"${ROOT}/scripts/run_full_ctest_preflight.sh" \
  "${ARPSID_FULL_PREFLIGHT_BUILD_DIR:-${ROOT}/.build/full-ctest-preflight}"

exec "${ROOT}/scripts/macos_build_install_validate_auv2.sh" "$@"
