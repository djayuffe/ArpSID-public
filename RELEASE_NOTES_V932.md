# ArpSID v932 — effective ARP authority final closure

This release closes the remaining effective-authority drift after the v927-v931 SynthMode/Instrument work.

## Fixed

- AU3 `runtimeIsArpEnabled()` now reports ARP as active only when ARP is actually eligible to own notes.
- Phase2 runtimeModel `arpActiveFlag` now mirrors effective render-mode authority rather than raw `kParamArpEnable`.
- Stale `ArpEnable=1` no longer leaks into SynthMode/SID-register or DrSID telemetry/control paths as active authority.

## Preserved

- v927 AU3 internal SynthMode note routing.
- v928 SynthMode-before-ARP authority.
- v929 Phase2 SynthMode/ARP order.
- v930 Pure Instrument GUI/render policy.
- v931 Pure Instrument effective-mode lock.
