# ArpSID v914 release cleanup final closure

## Scope

v914 is a source-tree cleanup and final hardening pass on top of v913. It does not change the musical/timing model except for one ingress-boundary hygiene fix.

## Fixed / cleaned

- Removed superseded one-off Logic AUv2 bootstrap audit probes `tools/audit_logic_auv2_bootstrap_v827.py` through `v830.py`; `v831` remains as the current probe.
- Tightened `rawMidiChannelVoiceLengthOk_()` so Program Change metadata is rejected at enqueue and cannot occupy `midiQueue_` as a dead event that is later discarded by the translator. Removed the now-dead `pendingProgramDirty_` ledger as well.
- Preserved v913 Phase2/AU3 `runtimeExecutionOwner_` null-guard law.
- Preserved v912/v911 async-ingress authority rules: accepted-only held mirroring, parent chunk drain guard, sorted chunk event merge, malformed MIDI rejection, and pre-canonical overflow telemetry.
- Regenerated `RELEASE_CONTENTS.sha256` after cleanup.

## Validation target

The release cleanup closure is guarded by the v910 ingress/timing-authority test source contracts plus the existing source-tree and audit-closure scripts.
