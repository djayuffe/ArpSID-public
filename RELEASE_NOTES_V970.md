# ArpSID v970 — test build-graph closure (handoff 22.7)

A build-infrastructure change on top of the v969 test-suite integrity closure. No production/runtime behavior changed and no shipped binary is affected.

Package: `0.0.690-pass380-v970-test-build-graph-closure`

## What changed

- **Compile the forensic patch bank once.** `source/forensic_patch_bank.cpp` is a heavyweight translation unit that **14 separate test executables were each recompiling from source**. It is now compiled once into a static library, `arpsid_forensic_patchbank`, which those 14 test targets link instead of re-adding the source. This cuts duplicate compilation and lowers peak build memory (14 parallel compiles of the same TU → 1) without weakening test isolation.
- `forensic_patch_bank.cpp` carries no per-target compile definitions, so a single shared object is ODR-correct for every consumer — verified by a clean full build with no duplicate-symbol/link errors.
- **Production wrappers are untouched.** The VST3 plugin and the AUv2 GUI smoke target continue to compile their own copy of the TU (separate binaries, ODR-safe), so no shipped artifact changes.

## Scope / measurements

- On this environment each TU compiles in ~1s, so the absolute clean-build win is modest (~13 fewer heavyweight compiles); the larger benefit is reduced peak memory and the correct object-library structure the handoff calls for — which matters more on the constrained CI the handoff describes.
- The AU-kernel header (`ArpSIDDSPKernel.hpp`, included by ~106 tests) was deliberately **not** touched: extracting it to a compiled unit is production surgery whose risk is not justified by the payoff here. That remains open under 22.7.

## Regression coverage

- `BuildGraphForensicPatchbankLibV970Tests` pins the invariant: the shared static library is defined from the single forensic TU, and every `*_tests` target that links it does **not** also recompile `source/forensic_patch_bank.cpp` (which would duplicate-compile and duplicate-link).

## Validation

- Clean full build with no duplicate-symbol/link errors; full CTest suite green (485 registered native tests + script guards). Version-coherence, source-tree and audit-closure guards pass. macOS AU/Logic, signing and real-SDK VST3 validation remain external sign-off items (unchanged).
