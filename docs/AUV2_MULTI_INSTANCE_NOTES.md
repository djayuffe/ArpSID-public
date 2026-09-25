# AUv2 multi-instance preset independence

This patch hardens AUv2/AUAudioUnit preset handling so multiple ArpSID AUv2 instances can keep different factory patches without sharing mutable preset metadata accidentally.

## What changed

- `ArpSIDAudioUnit` now owns an instance-local factory preset cache instead of using one process-global static preset array.
- The current factory preset metadata is now set through `_setCurrentFactoryPresetMetadataOnlyForSlot:` so each instance updates its own `currentPreset` object consistently.
- Factory startup now initializes patch 1 through the same per-instance preset path.
- AUv2 `kAudioUnitProperty_PresentPreset` now resolves against the instance's factory preset list first instead of always creating a synthetic preset object.
- AUv2 preset changes now notify both `ClassInfo` and `ClassInfoFromDocument` listeners so hosts that snapshot state per instance stay in sync.

## Why this matters

With multiple AUv2 instances in one host session, preset selection must stay instance-local. The audio state was already instance-owned, but preset metadata exposure was looser than it should be. This patch removes the remaining shared mutable preset surface and routes preset metadata back through the owning `ArpSIDAudioUnit` instance.


Additional invariant:
- AUv2 `ClassInfo` and `ClassInfoFromDocument` wrappers now persist and restore the instance-local preset number and preset name.
- Restore no longer depends on host-visible preset names alone; the owning AUv2 instance rebinds preset metadata from the wrapped state after applying the canonical blob.
- This prevents cross-instance preset metadata collapse when multiple AUv2 instances restore different patches in the same project.


Further hardening: AU state now persists the instance-local current factory preset slot in both fullState and fullStateForDocument, and restores metadata from the canonical state blob when the explicit key is absent. This prevents transport/timeline operations from falling back to stale or default preset metadata.
