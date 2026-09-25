# C64 / SIDCORE Full Integration Release

Version: `0.0.647-pass287`

This release completes the second integration pass over the C64 physical bus, CIA/VIC timing surface, SIDCORE `$D418` write bus, HLE KERNAL vectors, and the DIGI Pure 1:1 internal recorder.

## What changed

### CIA 6526

- Timer A/B now follow the requested decrement-then-detect rule:
  - decrement first
  - underflow only when the counter wraps to `$FFFF`
  - latch reload and one-shot stop occur at the underflow event
- Existing tests were updated from the older early-underflow model to the exact wrap-through-`$FFFF` contract.
- CNT mode is explicitly verified not to tick from PHI2 when CNT is unwired.
- TOD hour write normalization is now BCD-safe with 1..12 hour range and PM bit preservation.
- ICR read-clear remains bit-layout exact:
  - bits 0..4 are flags
  - bit 7 is synthesized from `(flags & mask)`
  - flags and IRQ line clear on read

### VIC-II

- Cleaned the remaining merged comment line in `c64_vic.h`.
- Existing `$D019/$D01A`, 9-bit raster compare, badline, and bus-steal tests remain passing.

### SIDCORE / `$D418`

- `$D418` capture remains a write-bus contract, not a changed-register filter.
- Repeated identical `$D418` values are preserved and counted.
- Absolute PHI2 timestamps are preserved in `C64SidBridgeTimedWrite`.
- `extractD418Samples(...)` and `reconstructD418Zoh(...)` remain the canonical helpers for internal DIGI reconstruction.

### DIGI “AU Pure 1:1 SID Engine” source

- C64/PSID render path now feeds internal Pure-SID REC from the timestamped C64 `$D418` bridge stream when `$D418` writes occur.
- Capture is zero-order-held per host frame.
- Repeated identical nibbles are preserved.
- The held `$D418` value persists across render blocks so sparse volume-register PCM does not collapse to silence between writes.
- This path is host-permission-free and independent of CoreAudio input, AudioQueue device capture, or output taps.

### HLE KERNAL / PSID no-ROM surface

`C64Platform::installPsidSafeVectors()` now exposes the full minimal HLE vector surface:

- `$FF48`: `PHA; TXA; PHA; TYA; PHA; JMP ($0314)`
- `$FE43`: `JMP ($0318)`
- `$EA31`: `LDA $DC0D; PLA; TAY; PLA; TAX; PLA; RTI`
- `$FE47`: `LDA $DD0D; RTI`
- `$FF8D`: RESTOR-style vector copy from `$FD30` to `$0314`
- `$E000`: reset stub that sets `$00=$2F`, `$01=$37`, calls `$FF8D`, then `CLI; RTS`

The PSID CIA playback trampoline was updated to be compatible with the KERNAL-style `$FF48` register-save entry. It now acknowledges CIA1, calls the play routine, restores Y/X/A, and then `RTI`s through the original hardware IRQ frame.

### Bridge hygiene

- C64 bridge timed-write stale entries are now cleared before resetting `timedWriteCount`.
- This fixes a cleanup-order bug where the reset happened first, making the stale-clear loop a no-op.

## New regression test

- `source/tests/c64_full_sidcore_integration_v708_tests.cpp`

It verifies:

- CIA decrement-then-`$FFFF` underflow timing
- CNT mode does not silently tick from PHI2
- TOD hour normalization
- repeated `$D418` write preservation
- `$D418` PHI2 timestamp extraction
- color-RAM open-bus upper nibble through the memory matrix
- HLE KERNAL `$FF48`, `$FF8D`, and `$E000` shape

## Validation

CMake/CTest targets validated:

- `C64PsidCiaIrqServiceV499Tests`
- `C64CiaProperV618Tests`
- `C64CiaExtendedV619Tests`
- `C64CiaPhi2IntegrationV620Tests`
- `C64D418DigiCaptureV706Tests`
- `C64OpenBusVicHleV707Tests`
- `C64FullSidcoreIntegrationV708Tests`

Broad source-level C64 sweep also passed for the modified timing/HLE surfaces.
