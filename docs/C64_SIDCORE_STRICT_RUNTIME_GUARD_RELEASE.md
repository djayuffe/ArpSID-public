# PASS294 — C64/SIDCORE strict runtime guard release

This release closes the next low-level C64/SIDCORE audit layer after pass293.

## Fixed

- `MemoryMatrix::cpuWrite()` now enforces the physical SID writable register window before dispatching to the PHI2 SID sink.
  - Writable: local SID registers `$00-$18` (`$D400-$D418`).
  - Non-writable/readback/open-bus: `$19-$1F` (`$D419-$D41F`).
  - Dropped write attempts are counted by `MemoryMatrix::droppedSidHoleWrites()`.
- `C64RuntimeSidSink`, `C64RuntimePhi2SidSinkBridge`, and `C64SidBridgeState` now reject `$D419-$D41F` write attempts even if a caller bypasses `MemoryMatrix`.
- RSID exactness now exposes two additional downgrade bits:
  - `LegacyCpuPlayback`: strict RSID attempted to use the legacy instruction-atomic CPU playback path.
  - `SidHoleWrite`: C64 code attempted to write to SID readback/hole registers.
- Strict RSID runtime no longer silently falls back to legacy instruction-atomic playback when the PHI2 machine is unavailable; it returns an incomplete run result and records the downgrade.
- Legacy `Mos6510` decimal ADC/SBC now uses the same final-result N/Z policy as `Cpu6510Micro`, removing a split-brain status-register difference between CPU paths.

## New regression tests

- `c64_sid_hole_write_gate_v717_tests.cpp`
  - Verifies `$D419-$D41F` do not reach the MemoryMatrix SID sink.
  - Verifies bridge/runtime sink-level gates also reject these writes.
- `c64_legacy_decimal_parity_v718_tests.cpp`
  - Verifies legacy decimal ADC/SBC N/Z flags are derived from the adjusted final result, matching `Cpu6510Micro` policy.

## Remaining truth boundary

This pass makes the implementation more honest and safer, but it does not claim transistor-perfect VIC/CIA/SID behavior. Known approximations remain surfaced through exactness downgrade bits, especially CIA cycle-exactness, VIC bus-steal approximation, fallback ROMs, and SID readback approximations when live SID oscillator/envelope state is unavailable.
