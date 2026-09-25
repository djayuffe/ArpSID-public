# ArpSID v923 — Classic Mode Authority Final Clean Closure

Version: `0.0.690-pass380-v923-classic-mode-authority-final-clean-closure`

This release is a final cleanup pass on the v919-v922 Classic/Synth reset-authority closure.

## Closed in v923

- Removed structural render-mode parameters from the DrSID transport kit replay list.
  - `kParamSynthModeEnable` is no longer part of `kDrSidTransportAuthorityParams`.
  - `kParamDrSidEnable` is no longer part of `kDrSidTransportAuthorityParams`.
- The DrSID transport authority replay list now contains only the 17 non-mode DrSID kit/model sound parameters.
- Structural mode authority is owned only by the explicit DrSID/SID808 and SynthMode reset arbitration blocks.
- This avoids future drift where render-mode bits could be accidentally replayed as ordinary kit data.
- Added source-contract coverage to keep mode parameters out of the DrSID kit replay list.

## Preserved closures

- v904-v908 projection/timing/no-output closures remain release-forward.
- v909-v922 Classic reset authority closures remain preserved.
- v910 ingress authority source identity remains preserved; the heavy AU3 test target is still too slow for this sandbox cold-build environment.
