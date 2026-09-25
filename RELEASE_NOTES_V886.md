# ArpSID 0.0.690 pass380 v886 — Audio Clock Authority Closure

Final split-clock closure after v885.

## Fixed

- C64 SIDPLAY audio write/sample PHI2 mapping no longer uses `c64TelemetryHostSampleCursor_`.
- Added dedicated audible cursor `c64PsidAudioHostSampleCursor_`.
- Audible cursor resets on C64 handoff and advances in every actual C64 audio rendered block/chunk.
- Telemetry cursor remains cosmetic-only for mirror publishing.
- Fractional-capable canonical SID backends now use one physical cycle-clock render law regardless of whether events have resolved-cycle metadata.
- VST ProcessContext is ingested once through the canonical transport path; legacy direct ProcessContext tempo/PPQ writes were removed.
- C64 PSID chunker derives the current play cadence before the first inner render instead of relying on a one-block-stale cache.

## Tests added

- `C64SidplayAudioClockV886SourceTests`
- `SidRuntimePhysicalClockLawV886SourceTests`

## Verified

Targeted v874-v886 closure tests pass, plus `DspKernelIncludeSmokeV633Tests` compiles the AU DSP kernel header.
