# ArpSID v929 — Phase2 SynthMode/ARP authority closure

This source-only closure completes the v927/v928 Classic/Synth authority work by applying the same SynthMode-first note-authority law to the Phase2/VST virtual-gate path.

## Fixed

- Phase2/VST virtual-gate can no longer let stale `ArpEnable` capture playable SynthMode/SID-register notes.
- `arpMode` is now disabled while top-level render mode is SynthMode.
- Phase2 virtual-gate NoteOn routes SynthMode before ARP fallback.
- Phase2 virtual-gate NoteOff releases SynthMode before ARP fallback.
- `ClassicModeAuthorityClosureV909Tests` now guards Phase2/VST authority order, not only AU3/internal and canonical host MIDI.

## Validation

- `RELEASE_CONTENTS.sha256`: OK
- `scripts/verify_source_tree.py`: OK
- `scripts/check_audit_closure.py`: OK
- `IngressParityTimingAuthorityV910Tests`: PASS
- `ClassicModeAuthorityClosureV909Tests`: PASS
