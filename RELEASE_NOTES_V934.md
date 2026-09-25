# ArpSID v934 backend/telemetry effective ARP closure

This release closes the remaining backend and telemetry-side ARP authority drift after v932/v933.

## Fixed

- Backend projection no longer enables the arpeggiator engine from raw stale `ArpEnable=1` while SynthMode/SID-register or DrSID owns top-level note authority.
- Tempo-linked ARP sync now runs only when ARP is effective authority.
- AU3 telemetry publishes `arpEnabled` from effective authority, not raw parameter cache.
- AU3 ARP-step telemetry is reset to 0 when ARP is not the effective authority.
- Phase2 full telemetry now gates `arpEnabled` by top-level render mode.

## Preserved

- v927 AU3 internal SynthMode authority.
- v928 SynthMode-before-ARP authority.
- v929 Phase2 SynthMode-before-ARP authority.
- v930/v931 Instrument GUI/render/effective-mode authority.
- v932/v933 effective ARP runtime and GUI presentation closures.
