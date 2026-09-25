# v928 SynthMode/ARP authority final closure

This release closes the remaining Classic/Synth authority ambiguity after v927.

## Fixed

- Canonical host MIDI routing now gives SynthMode/SID-register authority priority over stale `ArpEnable`.
- AU3 internal/virtual rendered-note routing now gives SynthMode/SID-register authority priority over stale `ArpEnable`.
- NoteOff routing mirrors NoteOn authority: SynthMode releases through `synthNoteOff()` / `kernelSynthNoteOff()` before ARP fallback.
- ARP remains a legacy/BitPerfect note authority unless/until it explicitly emits SID-register events.
- Added source-contract guards so SynthMode cannot regress behind ARP in either the shared canonical kernel or the AU3 internal helper.

## Preserved

- v927 AU3 internal SynthMode note routing.
- v926 TODO ledger contract.
- v925 full red-test closure.
- v924-v919 Classic/Synth/DrSID reset authority closures.
- v910 ingress parity and parent-scope timing authority closure.
