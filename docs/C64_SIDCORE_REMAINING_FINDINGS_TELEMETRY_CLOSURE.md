# PASS300 — C64/SIDCORE Remaining Findings Telemetry Closure

This pass closes the next concrete, testable layer from the C64/SIDCORE low-level audit.

## What changed

- Added explicit PSID-CIA compatibility-service telemetry:
  - `C64Runtime::psidCiaLegacyServiceCount()`
  - `C64Runtime::psidCiaCompatibilityServiceUsed()`
  - `C64Runtime::psidCiaPhysicalPhi2ServiceActive()`
- PSID-CIA playback through the legacy platform service is now counted so UI/release diagnostics cannot label it as strict/physical PHI2 execution.
- `runPsidCiaPlaybackServiceTicks()` and the PSID-CIA branch of `runPlay()` both increment the compatibility-service counter.
- Added a consolidated guard test that verifies:
  - strict RSID PHI2 path can be active while still downgraded,
  - SID `$D41B/$D41C` read approximation is ledger-visible,
  - missing/HLE ROM and CIA approximation remain ledger-visible,
  - PSID-CIA compatibility service is explicitly marked and not advertised as PHI2 physical.

## Honesty boundary

This pass does not claim transistor/cycle-exact CIA, VIC-II, SID readback, or real KERNAL ROM behavior. It makes the remaining known gaps more explicit and test-locked so host/UI code can distinguish:

- strict PHI2 RSID path active,
- known-downgrade-free,
- physically exact,
- PSID-CIA compatibility service.

## New test

- `source/tests/c64_remaining_findings_guard_v727_tests.cpp`
