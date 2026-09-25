# C64/SIDCORE final strict telemetry closure — pass298

This pass closes the next correctness layer raised by the pass296 audit.

## Closed

- Added explicit strict/path/exact naming:
  - `rsidStrictPhi2PathActive()` = active strict PHI2 path only.
  - `rsidKnownDowngradeFree()` = no observed downgrade bits.
  - `rsidPhysicallyExact()` = strict PHI2 path and downgrade-free.
  - `rsidExactPlaybackActive()` remains as deprecated compatibility spelling for `rsidPhysicallyExact()`.
- Added explicit 6510 jam reason telemetry:
  - `CpuJamReason::KilOpcode`
  - `CpuJamReason::UnsupportedOpcode`
  - `CpuJamReason::ApproximateOpcodeStrictPolicy`
  - `CpuJamReason::TrapBrkAsJam`
- Propagated jam reason into `C64Phi2Diagnostics` and `C64RunResult`.
- Fixed strict-policy propagation after `resetPhi2Machine_()`: strict RSID keeps `ApproximateOpcodePolicy::Jam` even after the PHI2 machine is rebuilt for init/load.
- Added regression coverage proving that strict approximate opcodes stop with `ApproximateOpcodeStrictPolicy`, not legacy fallback.

## Still intentionally honest

This release does not claim transistor-level CIA/VIC/SID/ROM exactness. Those remain surfaced through the exactness ledger (`CiaModelApprox`, `VicBusStealApprox`, `MissingRealRoms`, `SidReadApprox`, `OpenBusApprox`).
