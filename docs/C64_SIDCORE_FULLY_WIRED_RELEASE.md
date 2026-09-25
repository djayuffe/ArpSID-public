# PASS289 — C64/SIDCORE fully wired integration release

This pass continues the C64/SIDCORE closure beyond the $D418/processor-port/CIA2/VIC-bank work from pass286-pass288.

## Implemented

- Added `include/arpsid/core/c64_sid_trace_import.h`.
  - Defines a stable host/offline C64 SID trace event contract: `{absolute PHI2 cycle, chip, SID register, value}`.
  - Imports traces directly into `C64SidBridgeState`, preserving every write including repeated identical `$D418` writes.
  - Imports traces into the existing `SidWriteQueue` for authentic SID-core audition paths using deterministic PHI2-to-host-sample conversion.
  - Adds TS-compatible helpers for MIDI-to-SID trace generation:
    - `c64SidFrequencyRegisterFromHz(hz, clockHz)`
    - `c64SidTraceDefaultWaveForMidiChannel(channel)` with ch0=pulse, ch1=saw, ch9=noise drums, other=triangle.

- Hardened `C64SidBridgeState::resetTimedWrites()`.
  - Clears stale front entries before resetting counters.
  - Prevents later diagnostics/export paths from accidentally reading stale timed writes if they inspect the array while count is zero.

- Corrected physical SID readback when no live SID sink is attached.
  - Write-only SID registers no longer read back shadow writes.
  - `$D419/$D41A` return disconnected paddle high (`$FF`).
  - `$D41B/$D41C` are sourced from the attached live SID bridge when present; otherwise they fall back to open bus.
  - `$D418` and mirrored `$D438` remain on the physical C64 SID bus and keep the `$D418` capture contract.

## New regression tests

- `source/tests/c64_sid_trace_import_v710_tests.cpp`
  - Verifies all offline trace events import into the C64 SID bridge.
  - Verifies repeated identical `$D418` writes are preserved and counted.
  - Verifies multi-SID chip id and absolute PHI2 timestamps survive import.
  - Verifies `SidWriteQueue` import maps PHI2 cycles to host samples deterministically.
  - Verifies TS-compatible coarse MIDI wave mapping and frequency-register conversion.

- `source/tests/c64_sid_readback_physical_v711_tests.cpp`
  - Verifies write-only SID reads use physical open bus rather than shadow register RAM.
  - Verifies `$D419/$D41A` disconnected paddle reads.
  - Verifies `$D41B/$D41C` live bridge readback.
  - Verifies mirrored `$D438` is normalized to SID register `$18` and captured by the `$D418` bridge path.

## Validation

Built and ran the focused C64/SIDCORE CTest suite in the sandbox:

- `C64SidTraceImportV710Tests`
- `C64SidReadbackPhysicalV711Tests`
- `C64SidcoreCompleteIntegrationV709Tests`
- `C64FullSidcoreIntegrationV708Tests`
- `C64D418DigiCaptureV706Tests`
- `C64OpenBusVicHleV707Tests`
- `C64CiaProperV618Tests`
- `C64CiaExtendedV619Tests`
- `C64CiaPhi2IntegrationV620Tests`
- `C64Phi2ProcessorPortTests`
- `ReleaseRootCleanV705Tests`
- `SourceCommentCodeMergeGuardV704Tests`

Result: 12/12 pass.
