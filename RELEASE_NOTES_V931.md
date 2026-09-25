# ArpSID v931 — Instrument effective-mode final closure

This release closes the final Pure Instrument GUI/effective-mode authority edge left after v930.

## Fixed

- Pure Instrument `_effectiveModeIndexFromTelemetry()` now returns `SYNTH / SID REG` unconditionally.
- Stale telemetry, nil AU bridge state, or stale cached `SynthModeEnable=0` can no longer make the Instrument UI/reporting path resolve to `CLASSIC SID Player`.
- Deferred mode-tab sync after `_applyModeSelectionIndex()` cannot steer Pure Instrument back toward BitPerfect presentation.

## Preserved

- v927 AU3 internal SynthMode note authority.
- v928 SynthMode-before-ARP authority.
- v929 Phase2/VST virtual-gate SynthMode authority.
- v930 Instrument GUI/render authority.
