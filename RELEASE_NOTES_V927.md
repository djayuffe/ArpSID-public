# v927 — AU3 internal SynthMode authority closure

This release closes the remaining Classic/Synth internal-control authority gap.

## Fixed

- AU3 internal `handleNoteOn()` / `handleNoteOff()` no longer routes SynthMode notes into BitPerfect-only state.
- The shared internal rendered-note helper now constructs `SidTimedEvent` payloads and calls `kernelSynthNoteOn()` / `kernelSynthNoteOff()` when SynthMode/SID-register mode is active.
- SynthMode is no longer guarded behind BitPerfect engine availability in the AU3 internal note path.
- AU3 and AUv2 Instrument flavor state-root policy now persists:
  - `SynthModeEnable = 1`
  - `DrSidEnable = 0`
  - `ArpEnable = 0`
- AU3 Instrument GUI sync no longer infers render mode from a stale GUI cache. Pure Instrument is locked to `SYNTH / SID REG`.
- Mode labels now disambiguate:
  - `CLASSIC SID Player`
  - `SYNTH / SID REG`
  - `DR SID Drums`

## Preserved

- v926 TODO ledger contract closure.
- v925 red-test full closure.
- v924-v919 Classic/Synth/DrSID reset authority closure chain.
- v910 ingress parity source closure and v904-v909 timing/music/authority closure lineage.
