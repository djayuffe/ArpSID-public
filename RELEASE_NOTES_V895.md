# ArpSID 0.0.690 pass380 v895 — SID-core split-brain closure (comparator unification)

Deeper SID-core audit focused on cross-engine parity: the audio `SIDVoice`,
the `SidRegisterEngine`, and the C64 `SidReadbackModel` must implement the
same chip laws or C64 software that *reads* the SID ($D41B/$D41C polling)
observes a different chip than the one being rendered.

## Fixed — OSC3 pulse readback split-brain (missing 6581 comparator bias)

The SID pulse comparator law existed as THREE separately maintained copies.
`SIDVoice::generatePulse12` and the register engine's
`sidRegisterPulseComparator12` agreed (including the 6581 comparator bias:
PW≤$020 behaves +1, PW≥$F00 behaves +2, modeling comparator leak/asymmetry
without mutating register state). The `SidReadbackModel` copy had silently
dropped the bias, so **$D41B OSC3 polling of a 6581 pulse at extreme widths
disagreed with the rendered audio comparator** by 1–2 accumulator steps per
cycle — a genuine split-brain between what played and what the 6510 read.

All three engines now delegate to a single canonical
`sidPulseComparator12()` in `sid_combined_wave_model.h`, which also carries
the v854/v855 PW=$000 (constant high) / PW=$FFF (1/4096-duty spike) edge-case
law and the v893 TEST-forces-high law. Drift between engines is now
impossible by construction (same pattern as the v864 FilterCore closure).

## Fixed — dead law copies removed (drift incubators)

- `SIDVoice`'s private `blendNeighborBits12` / `smooth12Tap` /
  `bitWeightedLadder12` — zero callers; the live combined-wave law is
  `sidAnalogCombined12_Ultra` + the shared `sidCombined*` helpers.
- The register engine's local `sidBlendNeighborBits12` / `sidSmooth12Tap` /
  `sidBitWeightedLadder12` and the legacy non-Ultra `sidAnalogCombined12()`
  — zero callers.

Dead duplicates of an audio law are exactly how split-brains are born (edit
one copy, other engines keep the old behavior); removed rather than pinned.

## Audited clean (no change needed)

- **Sync/ring topology**: all three engines already share
  `kSidHardSyncSourceOf` / `sidPhaseMsbRose` / `sidHardSyncShouldReset`
  (single authority, including the no-cascade law).
- **Noise**: identical LFSR taps (22,20,16,13,11,7,4,2), identical feedback
  (b22^b17), identical output-bit mapping in all three engines; the two
  rise-count formulas (delta-based vs wrap-branch) are equivalent for every
  reachable per-cycle input (freq ≤ $FFFF < half a bit-19 period).
- **Envelope**: all three engines use the one `Sid6581Envelope` core; the
  register engine's `is6581 = !(sysByte & 0x02)` matches the kernel's
  system-byte convention (bit 1 set = 8580).
- **OSC3/ENV3 byte semantics**: all engines read pre-DAC digital values
  (`mix12 >> 4`, `env.dacOutput()`), matching hardware.
- **Register sanitization**: PW-HI masked to 4 bits, FC-LO to 3 bits — the
  hardware register aliasing, applied once at the write surface.

## Documented deviation (bounded, deliberately not plumbed)

`SidReadbackModel` calls the combined-wave model with fixed context
(35 °C / 5.0 V / default revision) while the audio engines use the live
forensic config. The defaults agree; divergence appears only when the user
changes revision/temperature AND a tune polls OSC3 on a *combined* waveform
(noise polling — the common RNG case — is exact). The readback model is
instantiated in 11 places across bridge/sink/runtime including their
snapshot/rollback state; plumbing live forensic context through all of them
is out of proportion to this bounded cosmetic divergence. Revisit only if a
real tune is found that polls combined-wave OSC3 under non-default forensics.

## Added tests

- `SidPulseComparatorParityV895Tests`:
  - canonical-law edge cases (PW $000/$FFF, both bias windows, TEST);
  - register-engine wrapper equals canonical law over a PW×phase sweep;
  - `SIDVoice` OSC readback equals canonical law via snapshot-restored
    phases on both models;
  - `SidReadbackModel` OSC3 now applies both 6581 bias windows (the exact
    pre-v895 divergence, pinned from the PHI2-clocked read surface).

## Verification

- `SidPulseComparatorParityV895Tests` PASS.
- Combined regression sweep (C64/PSID/RSID/PHI2/CPU + projection/synth +
  SID/forensic families, incl. v893/v894/v895 suites): **184/184 PASS**
  (3 suites require their CMake-declared `forensic_patch_bank.cpp` link, as
  always).
- AUv2 rebuild/install/clear-cache/strict-auval: see STATUS.md for the
  installed hash and per-subtype auval results of this pass.
