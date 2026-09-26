#!/usr/bin/env bash
# Mirror this (private) ArpSID checkout into a clone of the public repository.
#
# usage: scripts/sync_public_mirror.sh <path-to-ArpSID-public-clone> [--commit] [--push]
#
# Copies every tracked file at HEAD into the public clone (deleting files
# that no longer exist here) EXCEPT the C64 ROM files, which differ on
# purpose: the public repository ships a zero-filled placeholder instead of
# the real Commodore ROM images. Refuses to continue if the result would
# publish real ROMs.
#
#   --commit  commit the mirrored tree in the public clone, reusing this
#             repository's HEAD commit message
#   --push    also push the public clone's current branch (implies --commit)
#
# Pushing a VERSION.txt change to the public main branch triggers the
# Release workflow there.
set -euo pipefail

usage() { sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

PUBLIC=""; COMMIT=0; PUSH=0
for arg in "$@"; do
  case "$arg" in
    --commit) COMMIT=1 ;;
    --push)   COMMIT=1; PUSH=1 ;;
    -h|--help) usage 0 ;;
    -*) echo "unknown option: $arg" >&2; usage 2 ;;
    *) PUBLIC="$arg" ;;
  esac
done
[ -n "$PUBLIC" ] || usage 2

PRIVATE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PUBLIC="$(cd "$PUBLIC" && pwd)"

# Files that intentionally differ between the private and public trees.
KEEP_PUBLIC=(
  include/arpsid/core/c64_embedded_roms.h
  include/arpsid/core/c64_embedded_rom_loader.h
  source/tests/c64_embedded_rom_integration_v687_tests.cpp
)

die() { echo "sync_public_mirror: $*" >&2; exit 1; }

[ "$PRIVATE" != "$PUBLIC" ] || die "public path is this repository"
git -C "$PUBLIC" rev-parse --git-dir >/dev/null 2>&1 || die "$PUBLIC is not a git clone"
[ -z "$(git -C "$PRIVATE" status --porcelain)" ] || die "private tree has uncommitted changes; commit first"
[ -z "$(git -C "$PUBLIC" status --porcelain)" ] || die "public clone has uncommitted changes"
"$PRIVATE/scripts/ci/check_public_tree.sh" "$PUBLIC" >/dev/null \
  || die "public clone does not hold the ROM placeholder; refusing to touch it"

# Warn when this commit changed a ROM-specific file: it must be ported by hand.
changed_rom_files="$(git -C "$PRIVATE" diff --name-only HEAD~1 HEAD -- "${KEEP_PUBLIC[@]}" 2>/dev/null || true)"
if [ -n "$changed_rom_files" ]; then
  echo "sync_public_mirror: WARNING: HEAD changed ROM-specific files that are NOT mirrored:" >&2
  echo "$changed_rom_files" | sed 's/^/  /' >&2
fi

is_kept() {
  local f
  for f in "${KEEP_PUBLIC[@]}"; do [ "$1" = "$f" ] && return 0; done
  return 1
}

# 1. Remove public files that no longer exist in the private tree.
private_files="$(git -C "$PRIVATE" ls-tree -r --name-only HEAD)"
while IFS= read -r f; do
  [ -n "$f" ] || continue
  is_kept "$f" && continue
  grep -qxF -- "$f" <<<"$private_files" || git -C "$PUBLIC" rm -q -- "$f"
done < <(git -C "$PUBLIC" ls-files)

# 2. Overlay every private file except the ROM-specific ones (git + tar only).
tar_excludes=()
for f in "${KEEP_PUBLIC[@]}"; do tar_excludes+=("--exclude=$f"); done
git -C "$PRIVATE" archive HEAD | tar -x -C "$PUBLIC" "${tar_excludes[@]}"

"$PRIVATE/scripts/ci/check_public_tree.sh" "$PUBLIC" \
  || die "mirrored tree failed the public-tree guard; NOT committing (inspect $PUBLIC)"

git -C "$PUBLIC" add -A
if git -C "$PUBLIC" diff --cached --quiet; then
  echo "sync_public_mirror: public clone already matches $(git -C "$PRIVATE" rev-parse --short HEAD)"
  exit 0
fi
git -C "$PUBLIC" diff --cached --stat | tail -1

if [ "$COMMIT" -eq 1 ]; then
  git -C "$PRIVATE" log -1 --format=%B HEAD | git -C "$PUBLIC" commit -q -F -
  echo "sync_public_mirror: committed $(git -C "$PUBLIC" rev-parse --short HEAD) in $PUBLIC"
  if [ "$PUSH" -eq 1 ]; then
    git -C "$PUBLIC" push origin HEAD
  fi
else
  echo "sync_public_mirror: changes staged in $PUBLIC (re-run with --commit or commit there)"
fi
