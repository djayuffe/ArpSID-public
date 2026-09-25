# 0.0.652-pass292 — C64/SIDCORE physical SID holes completion release

This pass closes the remaining physical SID register-window correctness gap found
while auditing the C64/SIDCORE integration chain after pass291.

## Root cause

The previous helper treated `$D41D-$D41F` (`reg 0x1D-0x1F`) as writable
"deterministic mirror" lanes. That is useful as a UI shadow concept, but it is
not a physical 6581/8580 bus contract. The real SID exposes writable registers
only at `$D400-$D418`; `$D419-$D41C` are readback lanes and `$D41D-$D41F` are
unmapped holes/open bus. Letting `$D41D-$D41F` reach live register-write paths
can create non-C64 state, stale HUD/export mismatches, or accidental writes to
undefined SID-core lanes.

A second split remained in `C64Platform`: pass291 fixed mirror fallback in the
PHI2 memory matrix, but the legacy platform-side `sidAddressToChipReg_()` still
only applied the primary `$D400-$D7FF` mirror fallback when exactly one SID was
configured. That meant unassigned primary mirrors could still disappear in the
legacy C64Platform path under multi-SID.

## Fixed

- `c64SidRegWriteable(reg)` is now physical C64/SID correct:
  - writable: `0x00-0x18` only (`$D400-$D418`)
  - non-writable/read/open-bus: `0x19-0x1F` (`$D419-$D41F`)
- Projection bridge no longer special-cases ENV3 (`0x1C`) as accepted.
- `C64Platform::sidAddressToChipReg_()` now mirrors primary SID across
  `$D400-$D7FF` even under multi-SID, after exact configured SID windows win.
- Tests with stale `$D41F writeable deterministic mirror` expectations were
  updated to the physical unmapped-hole contract.
- Added regression coverage for:
  - `$D41D-$D41F` not reaching the SID bridge,
  - `$D41D-$D41F` not mutating the SID register image,
  - projection rejecting ENV3 and SID holes,
  - `$D418` still accepted,
  - exact secondary `$D420+$18` still wins over primary mirror,
  - unassigned `$D458` falls back to primary `$D418` under multi-SID.

## New test

- `source/tests/c64_sid_unmapped_holes_v715_tests.cpp`
- CMake target: `arpsid_c64_sid_unmapped_holes_v715_tests`
- CTest name: `C64SidUnmappedHolesV715Tests`

## Validation

Built and ran targeted CMake/CTest coverage for the new physical SID-hole
contract, existing SID mirror/reset tests, readback tests, PSID register seeding,
SIDCORE timeline, release root cleanup, and source comment merge guard.
