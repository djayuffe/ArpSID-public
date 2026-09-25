# PASS293 — C64/SIDCORE VIC Raster Compare Completion Release

This release closes a physical VIC-II raster compare correctness bug found after the pass292 SID-hole/mirror work.

## Root cause

`VicII::updateRasterIrq_()` reduced the 9-bit raster compare value with `% rasterLines_` before matching it against the current raster line. That is not how the VIC-II behaves. `$D012` plus `$D011` bit 7 form a physical 9-bit compare value. If the compare value is outside the active PAL/NTSC raster range, it simply never matches.

On PAL, `$1FF` must never fire because PAL lines are `0..311`. The old modulo logic incorrectly mapped `$1FF` to `511 % 312 = 199`, creating a false raster IRQ.

## Fix

`updateRasterIrq_()` now compares the current raster line directly against the 9-bit compare value and no longer performs modulo wrapping.

## Validation

Added `source/tests/c64_vic_raster_compare_v716_tests.cpp`.

It verifies:

- `$D011` bit 7 + `$D012` form a real 9-bit raster compare.
- PAL `$1FF` does not modulo-fire at line 199.
- In-range `$100` fires and sets `$D019` bit 0 and master bit 7.
- `$D019` write-one-to-ack clears the IRQ line.

## Files touched

- `include/arpsid/core/c64_vic.h`
- `source/tests/c64_vic_raster_compare_v716_tests.cpp`
- `CMakeLists.txt`
- `README.md`
- `VERSION.txt`
