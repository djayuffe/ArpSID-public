# ArpSID 0.0.690 pass380 v873 Release Notes

## Summary

v873 closes the remaining "authority drift" items left after the v872 SIDPLAY
render-transaction closure, plus the GM percussion projection gaps. The theme is:
the PHI2 machine is the real executor, so every mirror/validator/telemetry surface
must read and publish PHI2-authoritative state — and the SID808 GM map must actually
shape the voice for the ClosedHat/Cowbell/Rim families instead of collapsing them to
one generic drum.

## Fixed — C64 runtime authority

- **Color RAM sync (P0).** PHI2 writes to `$D800-$DBFF` live in `MemoryMatrix::colorRam_`,
  not `ram_`. The platform mirror sync mirrored a stale `peekRam()` byte and
  `pokeMemory($D800)` never reached the visible `colorRam()`. Added
  `MemoryMatrix::peekColorRam()` and `C64Platform::pokeColorRam()` (journaled for
  rollback); the delta sync, full-fallback sync, and contamination-recovery reseed
  now route the `$D800-$DBFF` range through color RAM.
- **Pure interrupt-bootstrap validator (P0).** `validateInstalledInterruptBootstrap()`
  increments `interruptValidationFailureCount_` and was called twice per PHI2 tick in
  the PSID-CIA edge-capture loop, inflating the failure count by thousands per service
  call. Split into a `const` compute helper + a side-effect-free
  `peekInstalledInterruptBootstrap()`; the edge-capture loop uses the pure peek.
- **Direct CIA `runPlay()` transaction safety (P0).** The CIA-timed branch of
  `C64Runtime::runPlay()` now owns a render transaction and rolls back on failure,
  matching the VBI branch. The AU render path already wrapped it; a direct/public
  caller no longer leaks partial RAM/SID/`$D418` state on a failed service.
- **Inspection sync parity (P1).** RSID continuous execution now syncs RAM + inspection
  (was SID mirror only); PSID continuous, PSID VBI passive, and all VBI-play success
  paths now publish CPU/CIA/VIC inspection state.
- **`loadPsid()` publish order (P1).** `loaded_` is now published only after the last
  failure point (`configurePsidSidBases`), so a false return means "nothing loaded".
- **Parser SID contiguity (P1).** Non-contiguous multi-SID metadata (e.g. a third SID
  with no second) is rejected at parse time instead of failing later in the runtime.
- **RSID `runPlay()` frame cadence (P1).** Uses `C64TimingMath::psidVbiFrameCycles()`
  (physical VIC frame) instead of integer `PHI2/50`,`/60`, removing ~48 cyc/frame PAL drift.
- **CIA latch clamp telemetry (P1).** A live CIA1 Timer A latch below the 1000-cycle
  scheduling floor is now counted/recorded instead of silently falling back.
- **CIA Timer B write-intent counters (P1).** `timerBLatchWriteCount`/
  `timerBControlWriteCount` on `$06/$07/$0F`, matching the Timer A counters.

## Fixed — SID808 GM projection

- **Class-mismatch guard.** `sid808GMNoteOverride()` returns the user override
  unchanged for an Unsupported spec, so a mismatched note/class pair (e.g.
  `noteOn(ClosedHat, note=49)`) can no longer leak a Crash profile/frequency onto the
  wrong drum family.
- **ClosedHat / Cowbell / Rim families now consume their profiles.** New percussion
  profiles (`PedalHat`, `RideBell`, `AgogoHigh/Low`, `TriangleMute`, `Vibraslap`,
  `Claves`, `WoodBlockHigh/Low`) with profile-aware synthesis branches, so Pedal Hat,
  Tambourine, Cabasa/Maracas, Short Guiro, Ride Bell, High/Low Agogo, Mute Triangle,
  Vibraslap, Claves and High/Low Wood Block are distinct, audible voices instead of a
  generic tick / cowbell partial / max-frequency rim click. High Rim-family notes no
  longer clamp to SID max frequency.
- **GM `decayScale` reaches the tail.** `decayScale` now projects into the release
  nibble of `sustainRelease` as well as the decay nibble, so hats/cymbals with a zero
  decay nibble get audible tail-length differences (e.g. Crash 1 vs Crash 2).

## Guards

- `Sid808GMProjectionV873Tests` (renamed from V872)
- `Sid808GMProfilesV873Tests` (renamed from V872)
- `Sid808GMFamiliesV873Tests` (new: ClosedHat/Cowbell/Rim family coverage + mismatch guard)
- retained C64 runtime/CIA/color-RAM/rollback/parser guards

## Validation

- Targeted header compilation (Apple clang, `-std=c++17`): PASS for every touched file.
- Targeted CTest sources built + run green, covering all touched files: runplay/CIA
  transaction safety, multi-SID rollback, rollback behavioral, PSID parser robustness,
  render transaction, CIA/color-RAM/integration, RSID exact init, cycle-exact closure,
  and all three v873 GM tests.
- Full CTest suite and AUv2 build/install/auval: NOT run in this pass (full build
  exceeds the working environment's time budget). Run `./build.sh --release-check` and
  `./build.sh --install-auv2 --clear-au-cache` in a full build environment before ship.

## Notes

- Historical `vNNN P-x` fix-generation comments were intentionally left unchanged;
  they record when a fix landed, not the current release version.
- `RELEASE_CONTENTS.sha256` regenerated for the renamed/added files.

## Release Files

- Detailed closure doc: `SID808_GM_FAMILIES_AND_RUNTIME_AUTHORITY_CLOSURE_V873.md`
