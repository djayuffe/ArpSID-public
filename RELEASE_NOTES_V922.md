# ArpSID v922 — Classic reset authority final correctness closure

Version: `0.0.690-pass380-v922-classic-reset-authority-final-correctness-closure`

v922 closes the final Classic/Synth/DrSID reset-authority arbitration hole found after v921.

## Fixed

- Host/AU transport-reset snapshots are now treated as overlay data, not structural mode authority, whenever an explicit factory/sticky root is being restored.
- A stale contradictory host snapshot can no longer promote an explicit Classic Synth/SidRegister factory root into DrSID/SID808 authority.
- A stale contradictory host snapshot can no longer demote or flip an explicit DrSID/SID808 factory root into SynthMode authority.
- `resetPreservingHostParameterSnapshot()` now computes `snapshotModeAuthorityAllowed = !hasExplicitFactorySlot` and gates snapshot-derived mode authority behind it.
- Explicit factory structural mode authority now arbitrates DrSID vs SynthMode before pre-reset/snapshot hints:
  - explicit DrSID/SID808 root => DrSID authority wins;
  - explicit Classic Synth/SidRegister root => SynthMode authority wins;
  - no explicit root => live pre-reset state / full-state snapshot behavior is preserved.

## Preserved

- v921 mode-bit dedup: render-mode parameters are not replayed as ordinary DrSID kit data.
- v920 explicit DrSID slot/program preservation.
- v919 SynthMode stale-reset reassertion and Instrument-flavor SynthMode policy.
- v918 package-root closure, v917 schema import closure, v916 factory/forensic cleanup, and v910 ingress-authority lineage.

## Validation

Validated from the source tree:

- `scripts/verify_source_tree.py`: OK
- `scripts/check_audit_closure.py`: OK
- `RELEASE_CONTENTS.sha256`: OK after regeneration
- Focused closure CTest suite: 9/9 passed

The AU3-heavy `IngressParityTimingAuthorityV910Tests` target still exceeds this sandbox's cold-build timeout and is not marked as run in this release note.
