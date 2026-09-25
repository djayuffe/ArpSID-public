# v936 Instrument SEQ presentation authority closure

Status: COMPLETE.

- Pure Instrument state-root policy now persists `SeqEnable=0` in both AU3 and AUv2, matching SynthMode/DrSID/ARP structural authority.
- Pure Instrument render flavor enforcement now clears `SeqEnable` as well as `ArpEnable`, so render-time policy cannot leave stale SEQ active beside SynthMode.
- `_applyModeSelectionIndex()` now clears and writes both ARP and SEQ off whenever Pure Instrument or SID REG/SynthMode is selected.
- GUI presentation adds `_effectiveSeqAuthorityEnabledForModeIndex()` and uses it for the header line, so stale raw `SeqEnable=1` is not shown while SynthMode/Pure Instrument owns note authority.
- SEQ step view now uses telemetry `tel.seqEnabled` rather than raw cache, matching DSP effective SEQ authority.
- Preserves v927-v935 SynthMode/ARP/SEQ/backend/GUI authority closures.
