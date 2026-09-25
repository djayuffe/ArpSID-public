# ArpSID v915 source cleanup closure

## Scope

v915 is a source-release hygiene pass on top of v914. It intentionally makes no audio/render/timing behavior changes.

## Fixed / cleaned

- Removed stale disabled-gate comments for the old `SourceTreeHygiene` and `ReleaseCandidateGate` placeholders from `CMakeLists.txt`; active release hygiene coverage remains represented by the concrete release/source-tree test targets and scripts.
- Refreshed `TODO.md` so the shipped package no longer opens with old v871 history while keeping the required `Source-side closure: COMPLETE` release-guard marker.
- Preserved v914 Program Change enqueue rejection, v913 owner guards, and v912/v911 ingress-authority hardening.
- Regenerated `RELEASE_CONTENTS.sha256`.

## Validation target

The closure is guarded by source-tree verification, audit closure verification, release cleanup/dead-file guards, and the v910 ingress/timing-authority source-contract test.
