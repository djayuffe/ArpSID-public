# ArpSID v930 — Instrument GUI/render authority closure

Version: `0.0.690-pass380-v930-instrument-gui-render-authority-closure`

## Closed in v930

- Pure Instrument GUI mode application is now locked to `SYNTH / SID REG` for every incoming mode request.
- `_applyModeSelectionIndex(2)` in Instrument flavor no longer maps to `CLASSIC SID Player / BitPerfect`; it resolves to `SYNTH / SID REG`.
- Instrument GUI mode application clears stale `ArpEnable` cache and AU parameter state.
- Instrument render flavor enforcement now clears `ArpEnable` to match AU2/AU3 state-root policy.
- `gui_viewcontroller_wiring_v590_tests.cpp` now asserts the v927+ forced-index-1 contract instead of the removed stale-cache `instrumentMode` path.

## Preserved

- v927 AU3 internal SynthMode authority closure.
- v928 SynthMode-before-ARP canonical/AU3 authority closure.
- v929 Phase2/VST SynthMode-before-ARP authority closure.
