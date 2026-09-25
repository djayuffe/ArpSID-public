# ArpSID v939 — BitPerfect Authority Helper + Behavioral Closure

Status: COMPLETE.

- Adds shared effective authority helpers:
  - `sidEffectiveArpAuthorityFromLiveParams()`
  - `sidEffectiveSeqAuthorityFromLiveParams()`
- AU3 runtime, AU3 SEQ engine, AU3 telemetry, Phase2 runtime, Phase2 telemetry and Phase2 SEQ now use the shared helpers instead of duplicating raw flag/mode checks.
- Adds `BitPerfectClassicAuthorityV939Tests`, a behavioral guard proving that ARP/SEQ are effective only as explicit secondary authorities inside CLASSIC / BitPerfect, and stale raw ARP/SEQ are masked in SynthMode and DrSID.
- Preserves v937 first-class BitPerfect/Classic authority and v938 SEQ BitPerfect authority closures.
