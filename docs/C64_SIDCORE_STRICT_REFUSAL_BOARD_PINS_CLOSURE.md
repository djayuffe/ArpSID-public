# C64/SIDCORE Strict Refusal + CIA2 Board-Pin Closure (pass297)

Pass297 closes two concrete correctness leaks found after the strict-ledger pass.

## 1. Strict RSID direct-cycle refusal ledger

`C64Runtime::runRsidMachineCycles()` had one remaining ledger bug: if strict RSID was asked to run while the PHI2 machine was unavailable, it refused execution but incremented `legacyRuntimePlaybackCount_`. That made the exactness ledger claim `LegacyCpuPlayback` even though no legacy Mos6510 playback had actually run.

The strict refusal path now records only `StrictRsidNotPhi2` and returns an empty incomplete result. `LegacyCpuPlayback` remains reserved for actual compatibility playback through the legacy instruction-atomic CPU.

Regression: `source/tests/c64_strict_direct_phi2_refusal_v722_tests.cpp`.

## 2. CIA2 PA0/PA1 VIC bank wiring uses board pins, not generic CPU read

`MemoryMatrix::cpuWrite()` updated VIC bank selection after writes to `$DD00/$DD02` by calling `cia2.read(0x00)`. PRA reads are currently side-effect free, but this was the wrong abstraction: VIC bank is wired to CIA2 PA0/PA1 pins, i.e. output latch bits where DDRA=1 and pulled/external input bits where DDRA=0.

CIA now exposes side-effect-free board-pin views:

- `Cia6526::portAPins()`
- `Cia6526::portBPins()`

MemoryMatrix uses `cia2.portAPins() & 0x03` for VIC bank updates.

Regression: `source/tests/c64_cia2_vic_bank_pins_v723_tests.cpp`.

## Status

This pass does not claim transistor-exact CIA/VIC/SID. It closes two specific correctness holes and keeps strict/compatible exactness reporting honest.
