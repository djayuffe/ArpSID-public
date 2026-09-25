# ArpSID v903 — final timing/music contract closure

This pass closes the remaining block-boundary and split-clock issues found after v902.

## Fixed

- **P0 future-block SID writes stuck forever**
  - Added `runtimeEndFractionalBlock()` in the fractional consumer contract.
  - Rebased surviving block-local `SidWriteQueue` entries at canonical block end.
  - Delayed hard-restart/test/gate/glide writes that land at `sampleOffset >= frameCount` now become due in the next block instead of staying permanently stale.

- **P1 single cycle-budget authority**
  - `SIDChip::ensurePlannedSample_()` now budgets whole PHI2 cycles from nominal `clockFrequency / sampleRate`, matching `SidCycleClockState`.
  - Forensic jitter/thermal/ripple no longer creates a second hidden cycle-count authority inside SIDChip. Analogue/filter forensic state remains active, but the event/render cycle lattice is shared.

- **P1 synth-mode glide/portamento wiring**
  - AU3 live render now calls `scheduleSynthModeGlideWrites()` once per canonical block before dispatcher consumption.
  - Active glide state emits frequency low/high register writes over time instead of becoming metadata-only.

- **P1/P2 DrSID wavetable split-span timing**
  - Added `tickWavetableRunnersForCycles_()` and routed whole-cycle and cycle-completing split subphase spans through it.
  - DrSID wavetable microprogram time now follows the same cycle boundaries as SIDChip interval physics under mid-cycle event splitting.

- **P2 stale NoteOff policy**
  - Stale/old MIDI NoteOff events now apply at sample 0/cycle 0, matching the “already due” policy instead of extending note length to block tail.

- **Regression hygiene**
  - Updated the old release-gate timestamp regression to the v902 floor+epsilon timing law.
  - Added `ProjectionMirrorFinalClosureV903Tests`.

## Verified

- `ProjectionMirrorFinalClosureV903Tests` PASS.
- `ReleaseGateRegression` PASS.
- Focused timing/projection closure sweep PASS: 24/24.

