# SID808 GM Families + C64 Runtime Authority Closure (v873)

Detailed closure for the v873 pass. Companion to `RELEASE_NOTES_V873.md`.

## 1. C64 runtime authority (PHI2 is the executor)

| Area | File | Change |
|------|------|--------|
| Color RAM read | `include/arpsid/core/c64_memory_matrix.h` | `peekColorRam()` reads `colorRam_` (4-bit), not `ram_`. |
| Color RAM write | `include/arpsid/core/c64_platform.h` | `pokeColorRam()` writes the visible `colorRam_` and journals the pre-write value for rollback. |
| Sync routing | `include/arpsid/core/c64_psid_runtime.h` | `syncPlatformRamFromPhi2Writes_` (delta + full) and `resyncPlatformFromAuthoritativePhi2` route `$D800-$DBFF` through color RAM. |
| Pure validator | `include/arpsid/core/c64_platform.h` | `computeInterruptBootstrapValidation_()` (const) + `peekInstalledInterruptBootstrap()`; mutating `validate…()` keeps the failure counter. |
| Edge capture | `include/arpsid/core/c64_psid_runtime.h` | `capturePsidCiaPhi2Edges_` uses the pure peek (no per-tick counter inflation). |
| CIA runPlay | `include/arpsid/core/c64_psid_runtime.h` | CIA-timed branch owns a render transaction + rolls back on failure (parity with VBI). |
| Inspection sync | `include/arpsid/core/c64_psid_runtime.h` | RSID continuous (+RAM), PSID continuous, VBI passive, VBI-play success paths publish inspection state. |
| loadPsid | `include/arpsid/core/c64_psid_runtime.h` | `loaded_` published only after `configurePsidSidBases` succeeds. |
| Parser | `include/arpsid/core/psid_header.h` | Rejects non-contiguous multi-SID metadata (`BadSidAddress`). |
| RSID cadence | `include/arpsid/core/c64_psid_runtime.h` | `C64TimingMath::psidVbiFrameCycles(pal)` instead of `PHI2/50`,`/60`. |
| Latch telemetry | `source/au3/ArpSIDDSPKernel.hpp` | `c64CiaLatchClampCount_` / `c64LastClampedCiaLatch_` surface sub-1000-latch clamps. |
| Timer B intent | `include/arpsid/core/c64_cia.h` | `timerBLatchWriteCount` / `timerBControlWriteCount` on `$06/$07/$0F`. |

### Deliberate deviations from the audit's literal suggestions

- **P0-3 vector source.** The audit suggested reading the IRQ vector from
  `peekRam($0314/$0315)`. That is a *different* vector than the current
  `cpuIrqVector` (`$FFFE`, KERNAL-mapped) and raw `peekRam` ignores banking, so it
  would be a regression. The real harm (P0-2 counter inflation) is removed by the
  pure peek, which is itself the "side-effect-free read" P0-3 asked for; the vector
  source is unchanged.
- **CIA latch clamp.** Behavior is unchanged (sub-1000 latch still falls back to the
  default 50/60 Hz cadence); only telemetry was added. Clamping to 1000 would make
  those tunes play ~20× too fast.
- **VBI-play failure path.** Inspection sync was added to the success paths only;
  failure is handled by rollback, and syncing inspection on the failure path would
  risk incoherence after the RAM rollback.

## 2. SID808 GM percussion families

`sid808_gm_projection.h` maps GM notes → `Sid808PercProfile`; `sid808_engine.h`
shapes the voice per profile. Before v873, only Tom and OpenHat consumed their
profiles — ClosedHat/Cowbell/Rim fell through to a generic voice.

- **New profiles:** `PedalHat`, `RideBell`, `AgogoHigh`, `AgogoLow`, `TriangleMute`,
  `Vibraslap`, `Claves`, `WoodBlockHigh`, `WoodBlockLow`.
- **New note map:** 44→PedalHat, 53→RideBell, 58→Vibraslap, 67→AgogoHigh,
  68→AgogoLow, 75→Claves, 76→WoodBlockHigh, 77→WoodBlockLow, 80→TriangleMute
  (81 stays Triangle).
- **Engine branches:** profile-aware `ClosedHat`, `Cowbell` and `Rim` branches with
  `sid808ClosedHatAttackConfig_`, `sid808CowbellProfileConfig_`,
  `sid808RimProfileConfig_`. Plain Cowbell (note 56, Default profile) keeps the
  existing two-partial program.
- **Class-mismatch guard:** `sid808GMNoteOverride` returns the user override for an
  Unsupported spec, so a mismatched note/class cannot project onto the wrong family.
- **decayScale → release:** projected into the `sustainRelease` release nibble as well
  as `attackDecay`, so zero-decay-nibble hats/cymbals get real tail differences.

## 3. Verification

- Every touched header compiles clean (`clang++ -std=c++17`, Apple clang).
- Targeted CTest sources built + run green (runplay/CIA transaction safety, multi-SID
  rollback, rollback behavioral, PSID parser robustness, render transaction, CIA,
  color-RAM open-bus blocker, full sidcore integration, VIC fast toggle, RSID exact
  init, cycle-exact closure).
- `Sid808GMProjectionV873Tests`, `Sid808GMProfilesV873Tests`,
  `Sid808GMFamiliesV873Tests`: PASS. The families test asserts each of the 12
  ClosedHat/Cowbell/Rim GM instruments selects its profile, is audible, is not
  SID-max clamped, is pitch-distinct within its family, and that the class-mismatch
  guard blocks a Crash-onto-ClosedHat leak.
- Full CTest + AUv2 build/install/auval: run in a full build environment
  (`./build.sh --release-check`, `./build.sh --install-auv2 --clear-au-cache`).
