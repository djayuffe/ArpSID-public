# 0.0.656-pass296 — C64/SIDCORE strict ledger final closure

This pass closes the next correctness layer from the low-level C64/6510/CIA/VIC/PLA/SID-bus audit.

## Closed

- `LegacyCpuPlayback` now means actual legacy instruction-atomic playback was used.
  A strict RSID refusal because PHI2 is unavailable records `StrictRsidNotPhi2`, but it no longer falsely records `LegacyCpuPlayback`.
- Legacy `Mos6510` now records approximate illegal opcode execution for chip-dependent opcodes used by the compatibility path:
  - `ARR #` (`$6B`)
  - `XAA #` (`$8B`)
  - `AHX` (`$93/$9F`)
  - `SHY` (`$9C`)
  - `SHX` (`$9E`)
  - `TAS` (`$9B`)
- `C64Runtime::rsidExactnessDowngradeReasons()` now propagates legacy approximate-illegal and semantic-fallback counters into the same exactness ledger as the PHI2 CPU.
- Strict/compatible mode separation is now more honest:
  - strict non-PHI2 refusal: `StrictRsidNotPhi2`
  - real compatibility fallback/playback: `LegacyCpuPlayback` / `LegacyFallback`
  - approximate illegal execution: `ApproximateOpcode`
  - semantic fallback/unsupported legacy opcode: `UnsupportedOpcode`

## New regression test

- `source/tests/c64_legacy_approximate_opcode_ledger_v721_tests.cpp`

This locks the compatibility CPU's approximate-illegal telemetry so it cannot silently execute chip-dependent illegal opcodes without downgrading exactness.
