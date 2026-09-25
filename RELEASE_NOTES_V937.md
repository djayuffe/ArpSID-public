# ArpSID v937 BitPerfect/Classic authority closure

Status: COMPLETE.

This pass promotes CLASSIC / BitPerfect from an implicit fallback into an explicit structural authority.

Closed:

- GUI mode selection is now authority-selecting, not stale-generator preserving.
  - CLASSIC writes `SynthModeEnable=0`, `DrSidEnable=0`, `ArpEnable=0`, `SeqEnable=0`.
  - SYNTH/SID REG writes `SynthModeEnable=1`, `DrSidEnable=0`, `ArpEnable=0`, `SeqEnable=0`.
  - DR SID writes `SynthModeEnable=0`, `DrSidEnable=1`, `ArpEnable=0`, `SeqEnable=0`.
- AU3 BitPerfect direct-poly stuck-note cleanup now uses resolved render mode and effective ARP/SEQ authority, not raw stale flags.
- Transport reset now has an explicit BitPerfect/Classic structural authority block for factory roots with Synth/DrSID/ARP/SEQ off.
- BitPerfect structural reset rejects stale host snapshots carrying ARP/SEQ on and clears direct note state.
- C64SidPlayer AU3/AUv2 state-root flavor policy now persists `SeqEnable=0` together with Synth/DrSID/ARP off.
- Source-contract guards were extended for BitPerfect structural reset, direct-poly cleanup, C64 Seq root policy and GUI mode selection.

Preserves v927-v936 SynthMode, ARP, SEQ, Instrument, Phase2, backend telemetry and GUI presentation closures.
