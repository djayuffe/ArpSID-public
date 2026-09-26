#!/usr/bin/env bash
# Fail if a build log contains compiler or linker warnings attributed to
# ArpSID itself. Third-party code (the VST3 SDK, external/) is ignored.
#
# usage: check_build_warnings.sh <build.log> [source-root]
set -euo pipefail

LOG="${1:?build log path required}"
ROOT="${2:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"

# Compiler diagnostics carry the file path; keep only ArpSID's own sources.
# GCC/Clang: path/file.cpp:12:3: warning: ...
own="$(grep -E "(^|[[:space:]])(${ROOT}/)?(source|include|cmake)/[^:]+:[0-9]+(:[0-9]+)?: warning:" "$LOG" || true)"
# MSVC: X:\path\source\file.cpp(12,3): warning C4244: ...  (third-party paths skipped)
msvc="$(grep -E '[\\/](source|include|cmake)[\\/][^(]+\([0-9]+(,[0-9]+)?\): warning C[0-9]+' "$LOG" \
        | grep -viE '[\\/](vst3sdk|external)[\\/]' || true)"
# Linker warnings have no source path; any of them is ours to fix.
ld="$(grep -E '(^|[[:space:]])ld(64)?: warning:|^/usr/bin/ld: warning:| : warning LNK[0-9]+' "$LOG" || true)"
own="$own$msvc"

if [ -n "$own$ld" ]; then
  echo "check_build_warnings: FAIL: ArpSID build warnings in ${LOG}:" >&2
  printf '%s\n%s\n' "$own" "$ld" | sed '/^$/d' | sort -u | head -50 >&2
  exit 1
fi
echo "check_build_warnings: OK (no ArpSID compiler/linker warnings in ${LOG})"
