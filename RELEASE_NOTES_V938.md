# ArpSID v938 — SEQ BitPerfect Authority Closure

This release closes the remaining SEQ effective-authority split after v937.

## Fixed

- AU3 SequencerEngine now runs only when the resolved render mode is `BitPerfect`.
- AU3 render-loop `seqEnabled` now uses BitPerfect effective authority, not merely `not SynthMode`.
- AU3 telemetry reports `seqEnabled` only for BitPerfect effective authority.
- Phase2/VST sequencer runtime and full telemetry now use the same BitPerfect-only SEQ authority law.
- GUI `_effectiveSeqAuthorityEnabledForModeIndex()` now suppresses stale SEQ in DrSID and dedicated drum flavors, not only SynthMode/Pure Instrument.
- Source-contract guards were extended so SEQ cannot regress into DrSID/SynthMode presentation or runtime authority.

## Authority law

Top-level note authority remains:

1. DrSID
2. SynthMode / SID-register
3. CLASSIC / BitPerfect with optional explicit ARP/SEQ secondary authority
4. BitPerfect direct fallback
