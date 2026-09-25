# ArpSID v944 — post-batch authority canonicalization closure

v944 closes remaining same-block host-automation ordering edges after v943.

## Fixed

- AU3 now snapshots the resolved render mode before dirty-parameter flush and detects structural returns from SynthMode/DrSID to BitPerfect in the same block.
- AU3 post-flush canonicalization now clears ARP/SEQ not only outside BitPerfect, but also on the exact block that structurally returns to BitPerfect from SynthMode/DrSID.
- Phase2/VST now snapshots the resolved render mode before canonical timed automation executes.
- Phase2/VST now runs post-canonical structural authority cleanup after timed automation has executed, preventing same-block ordering like `SynthMode=0` then `ArpEnable=1` from re-arming ARP on the transition block.
- Added `AuthorityPostBatchCanonicalizationV944Tests` to guard effective helper behavior and source contracts for AU3/Phase2 post-batch canonicalization.

## Preserved

- v943 params/renderParams/runtimeModel split-brain closure.
- v942 DrSID/SID808 kit preservation.
- v939 BitPerfect/Classic authority helpers.
