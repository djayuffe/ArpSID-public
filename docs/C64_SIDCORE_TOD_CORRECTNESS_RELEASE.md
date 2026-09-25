# PASS290 — C64/SIDCORE TOD Correctness Closure

This release closes a physical 6526 TOD write-protocol bug found during the final C64/SIDCORE integration audit.

## Root cause

The CIA TOD write protocol was reversed. The previous model stopped TOD on a write to `$08` / tenths and resumed it on a write to `$0B` / hours. Real MOS 6526 behavior is the opposite:

- Writing `$0B` / HOURS stops the TOD clock.
- Writing `$08` / TENTHS restarts the TOD clock.
- Alarm writes selected by CRB bit 7 write alarm registers only and must not stop/restart the live TOD clock.

This matters for RSID tunes that program CIA TOD using the normal high-to-low order: hours, minutes, seconds, tenths. With the old ordering the TOD could run while partially programmed or freeze after the final tenths write.

## Fix

`include/arpsid/core/c64_cia.h` now implements the physical write sequence:

- `$0B` stores normalized BCD hour, stops TOD, clears TOD accumulators.
- `$0A` stores minutes while stopped.
- `$09` stores seconds while stopped.
- `$08` stores tenths, restarts TOD, clears TOD accumulators.
- Alarm-mode writes do not alter the running/stopped state.

The old tests in `c64_cia_extended_v619_tests.cpp` and `c64_cia_phi2_integration_v620_tests.cpp` were updated from the stale reversed expectation to the physical 6526 contract.

## New regression test

Added:

- `source/tests/c64_cia_tod_write_protocol_v712_tests.cpp`

It proves:

- TOD does not advance after HOURS write before TENTHS restart.
- Minutes/seconds remain stable while stopped.
- TOD resumes on TENTHS write and advances one tenth at PAL 50 Hz.
- Alarm-mode TOD writes do not stop/restart the live TOD clock.

## Validation

CMake/CTest was run for the C64/SIDCORE integration set, including CIA, memory, SID readback, `$D418`, trace import, open bus/VIC/HLE, and release guards.
