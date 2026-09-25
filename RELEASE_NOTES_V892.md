# ArpSID 0.0.690 pass380 v892 — sample-only priority closure

## Fixed

- Fixed the remaining sample-only event ordering hole in `SidTimedEvent::before()`.
- `subphase` is now treated as physical ordering metadata only when both events are cycle-stamped.
- Same-sample sample-only events now order by canonical priority and arrival order, not by unresolved/advisory subphase values.
- This preserves the v891 policy that sample-boundary destructive controls (`Panic`, `AllSoundOff`, `AllNotesOff`) preempt lower-priority same-sample events even if those events carry a lower subphase sentinel/advisory value.
- Cycle-stamped events still sort by physical cycle/subphase as before.

## Added tests

- `SidRuntimeSampleOnlyPriorityV892Tests`
  - Proves sample-only `Panic` beats sample-only `NoteOn` despite `Panic` using subphase `0xFF`.
  - Proves sample-only `AllSoundOff` beats sample-only `NoteOff` by priority, not subphase.
  - Proves cycle-stamped events still use subphase ordering.
  - Proves dispatcher applies sample-only priority before render spans.

## Verification

Targeted v874-v892 closure test run passed: 25/25.

Also built `arpsid_dsp_kernel_include_smoke_v633_tests` successfully.
