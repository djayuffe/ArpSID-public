# ArpSID v942 — transition kit preservation + factory authority closure

v942 is a hardening pass after v941. It closes the remaining risk that a render-mode transition could silence performance voices by destroying DrSID/SID808 backend kit state, and it canonicalizes DrSID/SID808 factory roots so raw ARP/SEQ bits do not ship inside first-class DrSID authority roots.

## Fixed

- Render-mode transitions now silence DrSID with `allNotesOff()` instead of backend `reset()` in the shared transition helper.
- Phase2/VST local mode-transition cleanup no longer calls full backend reset; it uses performance all-notes-off and then clears queues, FX/normalizer state and canonical runtime mirrors.
- AU3 mode-transition sequencer transport re-arm now requires effective SEQ authority after the transition, so a stale raw `SeqEnable` cannot re-arm while SynthMode or DrSID owns note authority.
- DrSID/SID808 factory defaults now keep sequencer pattern data authored but load with `ArpEnable=0` and `SeqEnable=0`.
- Factory schema restoration explicitly clears ARP/SEQ for DrSID/SID808/Digi schema roots.
- Added `RenderModeTransitionKitPreservationV942Tests` source/behavioral guard.

## Authority law preserved

- DrSID, SynthMode and BitPerfect are first-class top-level render authorities.
- ARP/SEQ are only effective secondary note authorities inside BitPerfect.
- Mode transitions are performance-note cleanup boundaries, not patch/kit destruction boundaries.
