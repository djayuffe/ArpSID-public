# 0.0.655-pass295 — C64/SIDCORE strict PHI2 completion release

This pass closes the next strict-runtime layer from the low-level C64/6510 audit.

## Main closure

- RSID `runPlay()` no longer requires a PSID-style `playAddress`. RSID is machine-driven and now advances one PAL/NTSC frame through `C64Phi2Machine` when PHI2 is ready.
- Strict RSID refuses to continue through instruction-atomic `Mos6510` playback when PHI2 is unavailable. This prevents hidden per-instruction timestamp collapse for SID, RMW, IRQ and `$D418` writes.
- Strict RSID init failure no longer silently falls back to legacy `Mos6510`. Current runtime compatibility mode also fails closed when PHI2 init/play is unavailable.
- RSID frame playback now raises the instruction budget enough to cover a full PAL/NTSC PHI2 frame instead of tripping the old 4096-instruction PSID play budget.
- Exactness reporting gained two explicit downgrade bits:
  - `OpenBusApprox` for deterministic open-bus/unmapped IO observations.
  - `StrictRsidNotPhi2` for strict RSID attempts that could not remain entirely on `C64Phi2Machine`.

## Tests added

- `source/tests/c64_strict_rsid_phi2_only_v719_tests.cpp`
  - proves strict RSID `runPlay()` advances with `playAddress == 0` through PHI2.
  - proves strict RSID refuses non-PHI2 playback.
  - proves strict RSID init failure does not fall back to legacy Mos6510.
- `source/tests/c64_exactness_ledger_v720_tests.cpp`
  - proves open-bus observations are propagated into the downgrade ledger.
  - verifies existing missing-ROM, CIA and VIC approximation bits remain visible.

## Status

This pass does not claim transistor-perfect CIA/VIC/SID analog behavior. It makes the strict/compatibility boundary much harder: strict RSID is now prevented from silently running on the legacy instruction-atomic CPU path, and observed approximations are surfaced in the exactness ledger.
