# RELEASE_NOTES_V919 — Classic Synth Reset Authority Closure

Version: `0.0.690-pass380-v919-classic-synth-reset-authority-closure`

## Fixed

- Closed the Classic/Synth reset authority regression where a stale AU/Logic transport-reset snapshot could demote an explicit SynthMode/SidRegister factory patch to BitPerfect/default slot 0.
- Added symmetric SynthMode structural authority reassertion beside the existing DrSID reset authority path.
- Preserved sticky preset slot, BankSlot mirror and Program mirror for SynthMode factory roots after reset.
- Added `isTransportResetStructuralAuthorityParam()` so transport-reset overlay skips structural authority params without weakening ordinary full-state restore/import paths.
- Instrument flavor now explicitly forces `SynthModeEnable=1` and `DrSidEnable=0`, matching Pure Instrument / Classic Synth behavior.

## Validation

- `ClassicModeAuthorityClosureV909Tests` includes v919 source-contract coverage for SynthMode reset authority, transport-reset structural filtering, and Instrument flavor SynthMode policy.
